//! The package sources.

mod apt;
mod dnf;
mod flatpak;
mod pacman;
mod snap;

pub use apt::AptBackend;
pub use dnf::DnfBackend;
pub use flatpak::{FlatpakBackend, Permissions};
pub use pacman::PacmanBackend;
pub use snap::SnapBackend;

use std::collections::HashMap;

use crate::package::PackageInfo;
use crate::process::{self, run_with_stdin};

/// "12.3 MB" from a byte count.
pub fn human_size(bytes: u64) -> String {
    const UNITS: [&str; 5] = ["B", "KB", "MB", "GB", "TB"];
    let mut size = bytes as f64;
    let mut unit = 0;
    while size >= 1024.0 && unit < UNITS.len() - 1 {
        size /= 1024.0;
        unit += 1;
    }
    if unit == 0 {
        format!("{size:.0} {}", UNITS[unit])
    } else {
        format!("{size:.1} {}", UNITS[unit])
    }
}

/// Installed packages by name, to mark search results and fill in updates.
pub(crate) fn by_name(packages: Vec<PackageInfo>) -> HashMap<String, PackageInfo> {
    packages.into_iter().map(|p| (p.name.clone(), p)).collect()
}

/// `base` followed by `names`, as owned arguments.
pub(crate) fn with_names(base: &[&str], names: &[String]) -> Vec<String> {
    base.iter().map(|s| (*s).to_owned()).chain(names.iter().cloned()).collect()
}

/// Rows of a tool's `--columns=…` output: tab-separated fields per line.
pub(crate) fn tab_rows(text: &str) -> impl Iterator<Item = Vec<&str>> {
    process::lines(text).map(|l| l.split('\t').collect())
}

/// Replaces a root-owned file (pacman.conf, an apt sources file) through
/// pkexec, so only that one write needs the password.
pub(crate) fn write_file_as_root(path: &str, lines: &[String]) -> bool {
    let content = format!("{}\n", lines.join("\n"));
    run_with_stdin("pkexec", &["tee", path], content.as_bytes(), process::SHORT).success()
}

pub(crate) fn read_lines(path: &str) -> Vec<String> {
    std::fs::read_to_string(path)
        .map(|t| t.lines().map(str::to_owned).collect())
        .unwrap_or_default()
}

/// The last `max` lines of a log file, newest first.
pub(crate) fn log_tail(path: &str, max: usize) -> Option<String> {
    let text = std::fs::read_to_string(path).ok()?;
    Some(text.lines().rev().take(max).collect::<Vec<_>>().join("\n"))
}

/// Each line indented, for a transaction plan.
pub(crate) fn indented<'a>(lines: impl Iterator<Item = &'a str>) -> Vec<String> {
    lines.map(|l| format!("  {}", l.trim())).collect()
}

#[cfg(test)]
mod tests {
    use super::human_size;

    #[test]
    fn sizes() {
        assert_eq!(human_size(512), "512 B");
        assert_eq!(human_size(1536), "1.5 KB");
        assert_eq!(human_size(5 * 1024 * 1024 * 1024), "5.0 GB");
    }
}
