//! Reading progress out of package-manager output as it streams, and
//! saying plainly why a privileged command failed.

use std::sync::LazyLock;

use regex::Regex;

/// What a command says it's doing, and how far along it is when it says.
#[derive(Debug, Clone, PartialEq)]
pub struct Progress {
    pub label: String,
    /// 0.0–1.0, when the tool reports counts or a percentage.
    pub fraction: Option<f64>,
}

fn sentence(text: &str) -> String {
    let text = text.trim();
    let mut chars = text.chars();
    match chars.next() {
        Some(first) => first.to_uppercase().chain(chars).collect(),
        None => String::new(),
    }
}

fn counted(label: String, current: &str, total: &str, offset: f64) -> Progress {
    let (current, total) = (current.parse::<f64>().unwrap_or(0.0), total.parse::<f64>().unwrap_or(0.0));
    Progress {
        label,
        fraction: if total > 0.0 {
            Some(((current - offset) / total).clamp(0.0, 1.0))
        } else {
            None
        },
    }
}

/// One line of output → progress, for the tools Athena drives:
/// pacman's "( 3/10) upgrading foo", dnf's "[ 3/10] …", APT's Status-Fd
/// "pmstatus:" lines, and Flatpak's and Snap's per-app lines.
pub fn parse_line(line: &str) -> Option<Progress> {
    static APT: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^(dl|pm)status:[^:]*:([\d.]+):(.*)$").unwrap());
    static PACMAN: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^\(\s*(\d+)/(\d+)\)\s+(.+)$").unwrap());
    static DNF5: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^\[\s*(\d+)/(\d+)\]\s+(.+)$").unwrap());
    static DNF4: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^(\w[\w ]*?)\s*:\s*(\S+)\s+(\d+)/(\d+)$").unwrap());
    static FLATPAK: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^(Installing|Updating|Uninstalling)\s+(\d+)/(\d+)\S*\s*(.*)$").unwrap());
    static FLATPAK_REF: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^(Installing|Updating|Uninstalling)\s+(?:app|runtime)/([^/\s]+)").unwrap());
    static SNAP: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^(\S+)\s.*\b(installed|refreshed|removed|reverted)$").unwrap());

    let line = line.trim();
    if line.is_empty() {
        return None;
    }
    if let Some(m) = APT.captures(line) {
        let percent = m[2].parse::<f64>().unwrap_or(0.0);
        return Some(Progress {
            label: sentence(&m[3]),
            fraction: Some((percent / 100.0).clamp(0.0, 1.0)),
        });
    }
    if let Some(m) = PACMAN.captures(line).or_else(|| DNF5.captures(line)) {
        return Some(counted(format!("{} — {} of {}", sentence(&m[3]), &m[1], &m[2]), &m[1], &m[2], 0.0));
    }
    if let Some(m) = DNF4.captures(line) {
        return Some(counted(format!("{} {} — {} of {}", sentence(&m[1]), &m[2], &m[3], &m[4]), &m[3], &m[4], 0.0));
    }
    if let Some(m) = FLATPAK.captures(line) {
        // "Installing 2/3" starts the second item: one item done.
        return Some(counted(format!("{} {} — {} of {}", &m[1], m[4].trim(), &m[2], &m[3]), &m[2], &m[3], 1.0));
    }
    if let Some(m) = FLATPAK_REF.captures(line) {
        return Some(Progress {
            label: format!("{} {}", &m[1], &m[2]),
            fraction: None,
        });
    }
    if let Some(m) = SNAP.captures(line) {
        return Some(Progress {
            label: sentence(&format!("{} {}", &m[1], &m[2])),
            fraction: None,
        });
    }
    None
}

/// APT's Status-Fd lines are for the progress bar, not for reading.
pub fn is_status_line(line: &str) -> bool {
    line.starts_with("dlstatus:") || line.starts_with("pmstatus:")
}

/// A plain explanation of pkexec's own failures, from a command's output.
pub fn explain_failure(output: &str) -> Option<&'static str> {
    if output.contains("No authentication agent found") || output.contains("textual authentication agent") {
        Some(
            "Couldn't ask for your password: no polkit authentication agent is running in this session. Install one \
             (for example polkit-gnome or polkit-kde-agent) and log in again, or start it from your compositor's autostart.",
        )
    } else if output.contains("Request dismissed") || output.contains("Not authorized") {
        Some("Cancelled — the password prompt was dismissed, so nothing was changed.")
    } else {
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pacman() {
        let p = parse_line("( 3/10) upgrading firefox").unwrap();
        assert_eq!(p.label, "Upgrading firefox — 3 of 10");
        assert_eq!(p.fraction, Some(0.3));
    }

    #[test]
    fn dnf() {
        assert_eq!(parse_line("[ 2/4] Upgrading firefox-131.0-1.fc44.x86_64").unwrap().fraction, Some(0.5));
        let p = parse_line("  Upgrading        : firefox-131.0-1.fc40.x86_64     3/10").unwrap();
        assert_eq!(p.label, "Upgrading firefox-131.0-1.fc40.x86_64 — 3 of 10");
    }

    #[test]
    fn apt() {
        let p = parse_line("pmstatus:firefox:62.5:Unpacking firefox (131.0)").unwrap();
        assert_eq!(p.fraction, Some(0.625));
        assert_eq!(p.label, "Unpacking firefox (131.0)");
        assert!(is_status_line("dlstatus:1:40.5:Retrieving file 2 of 5"));
    }

    #[test]
    fn flatpak_and_snap() {
        assert_eq!(
            parse_line("Updating app/org.mozilla.firefox/x86_64/stable").unwrap().label,
            "Updating org.mozilla.firefox"
        );
        assert_eq!(parse_line("Installing 2/4… org.kde.Platform").unwrap().fraction, Some(0.25));
        assert_eq!(parse_line("firefox 131.0 from Mozilla** refreshed").unwrap().label, "Firefox refreshed");
        assert!(parse_line("random chatter").is_none());
    }

    #[test]
    fn failures() {
        assert!(
            explain_failure("Error creating textual authentication agent: …/dev/tty")
                .unwrap()
                .contains("polkit")
        );
        assert!(explain_failure("Request dismissed").unwrap().starts_with("Cancelled"));
        assert!(explain_failure("error: target not found").is_none());
    }
}
