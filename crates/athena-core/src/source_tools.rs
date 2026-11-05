//! Flatpak user data and Snap management, independent of GTK.
use crate::{Backend, Command, backends::FlatpakBackend, process};
use std::{
    collections::HashSet,
    fs,
    path::{Path, PathBuf},
};

#[derive(Clone, Debug)]
pub struct FlatpakData {
    pub app: String,
    pub path: PathBuf,
    pub bytes: u64,
    pub installed: bool,
    pub has_data: bool,
    pub has_override: bool,
}

pub fn valid_app_id(id: &str) -> bool {
    id.contains('.')
        && !id.starts_with('.')
        && id
            .split('.')
            .all(|part| !part.is_empty() && part.bytes().all(|b| b.is_ascii_alphanumeric() || b"_-".contains(&b)))
}

/// Never traverse symlinks while measuring app-owned data.
pub fn directory_size(path: &Path) -> u64 {
    let Ok(metadata) = fs::symlink_metadata(path) else { return 0 };
    if metadata.file_type().is_symlink() {
        return 0;
    }
    if metadata.is_file() {
        return metadata.len();
    }
    fs::read_dir(path).into_iter().flatten().flatten().map(|e| directory_size(&e.path())).sum()
}

pub fn flatpak_data() -> Vec<FlatpakData> {
    let installed: HashSet<_> = FlatpakBackend.list_installed().into_iter().map(|p| p.name).collect();
    inspect_data(&FlatpakBackend::user_data_root(), &FlatpakBackend::user_overrides_root(), &installed)
}

pub fn flatpak_data_for(id: &str) -> Result<FlatpakData, String> {
    if !valid_app_id(id) {
        return Err("Invalid Flatpak application ID".into());
    }
    let path = FlatpakBackend::user_data_root().join(id);
    Ok(FlatpakData {
        app: id.into(),
        bytes: directory_size(&path),
        has_data: path.exists(),
        has_override: FlatpakBackend::user_overrides_root().join(id).exists(),
        installed: FlatpakBackend.list_installed().iter().any(|p| p.name == id),
        path,
    })
}

pub fn inspect_data(root: &Path, overrides: &Path, installed: &HashSet<String>) -> Vec<FlatpakData> {
    let mut names = installed.clone();
    for root in [root, overrides] {
        for entry in fs::read_dir(root).into_iter().flatten().flatten() {
            if let Some(id) = entry.file_name().to_str().filter(|id| valid_app_id(id)) {
                names.insert(id.to_owned());
            }
        }
    }
    let mut result: Vec<_> = names
        .into_iter()
        .filter(|id| valid_app_id(id))
        .map(|app| {
            let path = root.join(&app);
            FlatpakData {
                bytes: directory_size(&path),
                installed: installed.contains(&app),
                has_data: path.exists(),
                has_override: overrides.join(&app).exists(),
                app,
                path,
            }
        })
        .collect();
    result.sort_by(|a, b| a.app.cmp(&b.app));
    result
}

pub fn clear_data_at(root: &Path, id: &str) -> Result<(), String> {
    if !root.is_absolute() || !valid_app_id(id) {
        return Err("Invalid application data path".into());
    }
    let path = root.join(id);
    let metadata = match fs::symlink_metadata(&path) {
        Ok(value) => value,
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(()),
        Err(e) => return Err(e.to_string()),
    };
    if !metadata.is_dir() || metadata.file_type().is_symlink() {
        return Err("Application data is not a regular directory".into());
    }
    fs::remove_dir_all(&path).map_err(|e| e.to_string())
}

pub fn clear_flatpak_data(id: &str, overrides: bool) -> Result<(), String> {
    let running = process::run("flatpak", &["ps", "--columns=application"], process::SHORT);
    if !running.success() {
        return Err("Could not check running Flatpak applications. No data was removed.".into());
    }
    if running.stdout.lines().any(|line| line.trim() == id) {
        return Err("Close this application before clearing its data.".into());
    }
    clear_data_at(&FlatpakBackend::user_data_root(), id)?;
    if overrides {
        let result = FlatpakBackend::reset_overrides(id);
        if !result.success {
            return Err(result.output);
        }
    }
    Ok(())
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Connection {
    pub interface: String,
    pub plug: String,
    pub slot: String,
    pub notes: String,
}

pub fn parse_connections(text: &str) -> Vec<Connection> {
    text.lines()
        .filter_map(|line| {
            let fields: Vec<_> = line.split_whitespace().collect();
            if fields.len() < 3 || fields[0] == "Interface" {
                return None;
            }
            Some(Connection {
                interface: fields[0].into(),
                plug: fields[1].into(),
                slot: fields[2].into(),
                notes: fields.get(3).unwrap_or(&"").to_string(),
            })
        })
        .collect()
}

pub fn snap_connections(name: &str) -> Vec<Connection> {
    parse_connections(&process::run("snap", &["connections", name], process::SHORT).stdout)
}

pub fn snap_info(name: &str) -> String {
    process::run("snap", &["info", name], process::SHORT).combined()
}

fn safe_argument(value: &str) -> bool {
    !value.is_empty() && !value.starts_with('-') && value.bytes().all(|b| b.is_ascii_alphanumeric() || b"._:/+-".contains(&b))
}

pub fn snap_command(action: &str, name: &str, value: &str) -> Option<Command> {
    if !safe_argument(name) {
        return None;
    }
    let args = match action {
        "channel" if safe_argument(value) => vec!["snap".to_owned(), "refresh".into(), name.into(), format!("--channel={value}")],
        "revert" | "enable" | "disable" => vec!["snap".into(), action.into(), name.into()],
        "connect" if safe_argument(value) && name.contains(':') => vec!["snap".into(), "connect".into(), name.into(), value.into()],
        "disconnect" if name.contains(':') => vec!["snap".into(), "disconnect".into(), name.into()],
        _ => return None,
    };
    Some(Command::privileged(args))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn limits_data_removal_and_does_not_follow_links() {
        let root = std::env::temp_dir().join(format!("athena-data-test-{}", std::process::id()));
        let data = root.join("apps/org.example.App");
        let outside = root.join("outside");
        fs::create_dir_all(&data).unwrap();
        fs::create_dir_all(&outside).unwrap();
        fs::write(outside.join("keep"), "safe").unwrap();
        fs::write(data.join("data"), "12345").unwrap();
        #[cfg(unix)]
        std::os::unix::fs::symlink(&outside, data.join("link")).unwrap();
        assert_eq!(directory_size(&data), 5);
        assert!(clear_data_at(&root.join("apps"), "../outside").is_err());
        clear_data_at(&root.join("apps"), "org.example.App").unwrap();
        assert!(outside.join("keep").exists());
        fs::remove_dir_all(root).unwrap();
    }
    #[test]
    fn snap_interfaces_and_arguments() {
        let connections = parse_connections("Interface Plug Slot Notes\nhome example:home :home -\nnetwork example:network - manual");
        assert_eq!(connections.len(), 2);
        assert_eq!(connections[1].slot, "-");
        assert!(snap_command("connect", "example:home", ":home").is_some());
        assert!(snap_command("channel", "example", "latest/beta").is_some());
        assert!(snap_command("channel", "example", "--dangerous").is_none());
        assert!(snap_command("remove", "example", "").is_none());
    }
    #[test]
    fn catalog_actions_remain_bound_to_the_chosen_installation() {
        let commands = flatpak_commands("install", "org.example.App", "user:flathub-beta", "app/org.example.App/aarch64/beta").unwrap();
        assert!(commands[0].args.contains(&"--user".into()));
        assert_eq!(commands[0].args.last().unwrap(), "app/org.example.App/aarch64/beta");
        let remove = flatpak_commands("remove", "org.example.App", "installation=games:flathub", "").unwrap();
        assert!(remove.iter().all(|command| command.args.contains(&"--installation=games".into())));
        assert!(flatpak_commands("remove", "org.example.App", "unrecognized:flathub", "").is_none());
    }
}

/// Keep a selected catalog installation/architecture/branch bound to its action.
pub fn flatpak_commands(action: &str, package: &str, repository: &str, reference: &str) -> Option<Vec<Command>> {
    let (scope, remote) = repository
        .split_once(':')
        .map(|(scope, remote)| (Some(scope), remote))
        .unwrap_or((None, repository));
    let flag = match scope {
        Some("user") => Some("--user".to_owned()),
        Some("system") => Some("--system".to_owned()),
        Some(scope)
            if scope
                .strip_prefix("installation=")
                .is_some_and(|s| !s.is_empty() && s.bytes().all(|b| b.is_ascii_alphanumeric() || b"._-".contains(&b))) =>
        {
            Some(format!("--{scope}"))
        }
        Some(_) => return None,
        None => None,
    };
    let target = if reference.is_empty() { package } else { reference };
    let mut args = vec![
        match action {
            "install" | "reinstall" => "install",
            "remove" => "uninstall",
            "update" => "update",
            _ => return None,
        }
        .to_owned(),
        "-y".into(),
        "--noninteractive".into(),
    ];
    if let Some(flag) = &flag {
        args.push(flag.clone());
    }
    if action == "reinstall" {
        args.push("--reinstall".into());
    }
    args.push("--".into());
    if matches!(action, "install" | "reinstall") {
        if remote.is_empty() {
            return None;
        }
        args.push(remote.into());
    }
    args.push(target.into());
    let mut commands = vec![Command::new("flatpak", args)];
    if action == "remove" {
        let mut args = vec!["uninstall".to_owned(), "--unused".into(), "-y".into(), "--noninteractive".into()];
        if let Some(flag) = flag {
            args.push(flag);
        }
        commands.push(Command::new("flatpak", args));
    }
    Some(commands)
}
