//! Snapshot commands. Listing Timeshift is deliberately opt-in: it needs root.
use crate::{
    Command,
    process::{self, run},
};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Provider {
    Snapper,
    Timeshift,
}
impl Provider {
    pub fn detect() -> Option<Self> {
        if process::executable_exists("snapper") {
            Some(Self::Snapper)
        } else if process::executable_exists("timeshift") {
            Some(Self::Timeshift)
        } else {
            None
        }
    }
    pub fn name(self) -> &'static str {
        match self {
            Self::Snapper => "Snapper",
            Self::Timeshift => "Timeshift",
        }
    }
    pub fn create(self, description: &str) -> Command {
        match self {
            Self::Snapper => Command::privileged([
                "snapper",
                "--config",
                "root",
                "create",
                "--type",
                "single",
                "--cleanup-algorithm",
                "number",
                "--description",
                description,
            ]),
            Self::Timeshift => Command::privileged(["timeshift", "--create", "--comments", description, "--tags", "O", "--scripted"]),
        }
    }
    pub fn list_command(self) -> Command {
        match self {
            Self::Snapper => Command::new(
                "snapper",
                ["--config", "root", "--csvout", "--no-headers", "list", "--columns", "number,date,description"],
            ),
            Self::Timeshift => Command::privileged(["timeshift", "--list", "--scripted"]),
        }
    }
    pub fn command(self, action: &str, id: &str) -> Result<Command, String> {
        let valid = match self {
            Self::Snapper => id.parse::<u64>().is_ok_and(|id| id > 0),
            Self::Timeshift => {
                id.len() == 19
                    && id.bytes().enumerate().all(|(i, b)| match i {
                        4 | 7 => b == b'-',
                        10 => b == b'_',
                        13 | 16 => b == b'-',
                        _ => b.is_ascii_digit(),
                    })
            }
        };
        if !valid {
            return Err("Choose a valid snapshot identifier (the current filesystem cannot be changed).".into());
        }
        match (self, action) {
            (Self::Snapper, "delete") => Ok(Command::privileged(["snapper", "--config", "root", "delete", id])),
            (Self::Snapper, "rollback") => Ok(Command::privileged(["snapper", "--config", "root", "rollback", id])),
            (Self::Snapper, "undo") => Ok(Command::privileged(vec![
                "snapper".into(),
                "--config".into(),
                "root".into(),
                "undochange".into(),
                format!("{id}..0"),
            ])),
            (Self::Timeshift, "delete") => Ok(Command::privileged(["timeshift", "--delete", "--snapshot", id, "--scripted"])),
            // The graphical restore wizard collects target disks and bootloader choices.
            (Self::Timeshift, "restore") => Ok(Command::new("timeshift-launcher", std::iter::empty::<&str>())),
            _ => Err("Unsupported snapshot action.".into()),
        }
    }
}
/// Never prompts for authentication during startup/dashboard refresh.
pub fn summary() -> String {
    match Provider::detect() {
        Some(Provider::Snapper) => {
            let command = Provider::Snapper.list_command();
            let out = run(&command.program, &command.args, process::SHORT);
            if out.success() {
                latest(Provider::Snapper, &out.stdout)
            } else {
                "Snapper: open Snapshots to check configuration or permissions".into()
            }
        }
        Some(Provider::Timeshift) => LAST_TIMESHIFT
            .lock()
            .ok()
            .and_then(|v| v.clone())
            .unwrap_or_else(|| "Timeshift: use the administrator button to list snapshots".into()),
        None => "Install and configure Snapper or Timeshift to use snapshots".into(),
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn refuse_current_or_injected_snapshot() {
        for bad in ["0", "-1", "1..0", "--all", "1;reboot"] {
            assert!(Provider::Snapper.command("delete", bad).is_err());
        }
        assert!(Provider::Snapper.command("undo", "12").unwrap().args.contains(&"12..0".into()));
        assert!(Provider::Timeshift.command("delete", "2026-11-04_10-22-30").is_ok());
        assert!(Provider::Timeshift.command("delete", "../../etc").is_err());
    }
}

/// Downloads, metadata refreshes and snapshot maintenance are not package
/// transactions. A serial list of mutations gets exactly one leading snapshot.
pub fn is_transaction(commands: &[Command]) -> bool {
    commands.iter().any(|command| {
        if command.args.iter().any(|a| a == "--execute-reviewed-aur") {
            return true;
        }
        let mut args = command.args.iter();
        let program = if command.program == "pkexec" {
            args.find(|a| !a.starts_with('-')).map(String::as_str).unwrap_or("")
        } else {
            &command.program
        };
        let args = args.map(String::as_str).collect::<Vec<_>>();
        match program {
            "pacman" => {
                !args.iter().any(|a| a.starts_with("-S") && (a.contains('w') || a.contains('c')))
                    && args.iter().any(|a| {
                        a == &"-S"
                            || a.starts_with("-Su")
                            || a.starts_with("-Syu")
                            || a.starts_with("-Syyu")
                            || a.starts_with("-R")
                            || a.starts_with("-U")
                            || ["--sync", "--remove", "--upgrade"].contains(a)
                    })
            }
            "apt" | "apt-get" | "dnf" | "dnf5" => args
                .iter()
                .any(|a| ["install", "remove", "purge", "reinstall", "upgrade", "autoremove", "distro-sync"].contains(a)),
            "flatpak" => args.iter().any(|a| ["install", "uninstall", "update"].contains(a)) && !args.contains(&"--no-deploy"),
            "snap" => args.iter().any(|a| ["install", "remove", "refresh", "revert", "enable", "disable"].contains(a)),
            _ => false,
        }
    })
}

static LAST_TIMESHIFT: std::sync::Mutex<Option<String>> = std::sync::Mutex::new(None);
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Snapshot {
    pub id: String,
    pub date: String,
    pub description: String,
}
pub fn parse_listing(provider: Provider, text: &str) -> Vec<Snapshot> {
    let mut snapshots = match provider {
        Provider::Snapper => csv::ReaderBuilder::new()
            .has_headers(false)
            .flexible(true)
            .from_reader(text.as_bytes())
            .records()
            .filter_map(|r| {
                let r = r.ok()?;
                let id = r.get(0)?.trim();
                if id.parse::<u64>().ok()? == 0 {
                    return None;
                }
                Some(Snapshot {
                    id: id.into(),
                    date: r.get(1).unwrap_or("").into(),
                    description: r.get(2).unwrap_or("").into(),
                })
            })
            .collect::<Vec<_>>(),
        Provider::Timeshift => text
            .lines()
            .filter_map(|line| {
                let fields: Vec<_> = line.split_whitespace().collect();
                let position = fields.iter().position(|v| v.len() == 19 && v.as_bytes().get(10) == Some(&b'_'))?;
                let id = fields[position];
                Provider::Timeshift.command("delete", id).ok()?;
                Some(Snapshot {
                    id: id.into(),
                    date: id.into(),
                    description: fields[position + 1..].join(" "),
                })
            })
            .collect(),
    };
    match provider {
        Provider::Snapper => snapshots.sort_by_key(|s| std::cmp::Reverse(s.id.parse::<u64>().unwrap_or(0))),
        Provider::Timeshift => snapshots.sort_by(|a, b| b.id.cmp(&a.id)),
    };
    snapshots
}
fn latest(provider: Provider, text: &str) -> String {
    parse_listing(provider, text)
        .first()
        .map(|s| format!("{}: {} · {} · {}", provider.name(), s.id, s.date, s.description))
        .unwrap_or_else(|| format!("{}: no snapshots", provider.name()))
}
pub fn record_listing(provider: Provider, text: &str) {
    if provider == Provider::Timeshift
        && let Ok(mut stored) = LAST_TIMESHIFT.lock() {
            *stored = Some(format!("{} (last administrator check)", latest(provider, text)));
        }
}
#[cfg(test)]
mod batch_tests {
    use super::*;
    #[test]
    fn snapshot_only_mutations() {
        for command in [
            Command::privileged(["pacman", "-Syu"]),
            Command::privileged(["apt-get", "install", "example"]),
            Command::new("athena", ["--execute-reviewed-aur", "manifest"]),
        ] {
            assert!(is_transaction(&[command]));
        }
        for command in [
            Command::privileged(["pacman", "-Sc", "--noconfirm"]),
            Command::privileged(["pacman", "-Sw", "example"]),
            Command::new("flatpak", ["update", "--no-deploy"]),
            Provider::Timeshift.list_command(),
            Provider::Snapper.create("test"),
        ] {
            assert!(!is_transaction(&[command]));
        }
    }
    #[test]
    fn parses_quoted_snapper_and_timeshift_rows() {
        let rows = parse_listing(Provider::Snapper, "0,,current\n8,2026-11-04,\"before, upgrade\"\n9,2026-11-05,\"two\nlines\"\n");
        assert_eq!(rows.len(), 2);
        assert_eq!(rows[0].id, "9");
        assert_eq!(rows[1].description, "before, upgrade");
        let rows = parse_listing(Provider::Timeshift, "Num Name Tags Description\n0 > 2026-11-04_10-20-30 O before install");
        assert_eq!(rows[0].id, "2026-11-04_10-20-30");
    }
}
