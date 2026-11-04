//! Flatpak. Not wrapped in pkexec: flatpak has its own polkit action, so a
//! plain `flatpak install` as the user asks for a password itself when a
//! system-wide change needs one.

use std::collections::{BTreeMap, HashMap, HashSet};
use std::path::PathBuf;

use super::{by_name, indented, tab_rows, with_names};
use crate::backend::{field, Backend};
use crate::package::{OperationResult, PackageInfo, RepositoryAddField, RepositoryInfo, TransactionPreview};
use crate::process::{self, executable_exists, run, Command};

pub struct FlatpakBackend;

/// An app's effective sandbox permissions (its own metadata merged with
/// user and system overrides).
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Permissions {
    /// "network", "ipc"
    pub shared: HashSet<String>,
    /// "x11", "wayland", "pulseaudio", "session-bus", "system-bus", "ssh-auth"
    pub sockets: HashSet<String>,
    /// "dri", "all", "kvm", "shm"
    pub devices: HashSet<String>,
    /// "devel", "multiarch", "bluetooth", "canbus", "per-app-dev-shm"
    pub features: HashSet<String>,
    /// As reported: "home", "host", "xdg-download:ro"
    pub filesystems: Vec<String>,
    pub session_bus_talk: Vec<String>,
    pub session_bus_own: Vec<String>,
    pub system_bus_talk: Vec<String>,
    pub system_bus_own: Vec<String>,
    pub env_vars: Vec<(String, String)>,
}

/// `flatpak info --show-permissions`: INI-shaped, with [Context], bus
/// policy, and [Environment] sections.
pub fn parse_permissions(text: &str) -> Permissions {
    let set = |v: &str| v.split(';').filter(|s| !s.is_empty()).map(str::to_owned).collect::<HashSet<_>>();
    let mut perms = Permissions::default();
    let mut section = String::new();
    for line in text.lines().map(str::trim).filter(|l| !l.is_empty()) {
        if line.starts_with('[') && line.ends_with(']') {
            section = line[1..line.len() - 1].to_owned();
            continue;
        }
        let Some((key, value)) = line.split_once('=') else { continue };
        let (key, value) = (key.trim(), value.trim());
        match section.as_str() {
            "Context" => match key {
                "shared" => perms.shared = set(value),
                "sockets" => perms.sockets = set(value),
                "devices" => perms.devices = set(value),
                "features" => perms.features = set(value),
                "filesystems" => perms.filesystems = value.split(';').filter(|s| !s.is_empty()).map(str::to_owned).collect(),
                _ => {}
            },
            "Session Bus Policy" | "System Bus Policy" => {
                let system = section.starts_with("System");
                let target = match (system, value.to_lowercase().as_str()) {
                    (false, "talk") => &mut perms.session_bus_talk,
                    (false, "own") => &mut perms.session_bus_own,
                    (true, "talk") => &mut perms.system_bus_talk,
                    (true, "own") => &mut perms.system_bus_own,
                    _ => continue,
                };
                target.push(key.to_owned());
            }
            "Environment" => perms.env_vars.push((key.to_owned(), value.to_owned())),
            _ => {}
        }
    }
    perms
}

fn installed_rows(text: &str) -> Vec<PackageInfo> {
    // --columns=name,description,application,version,branch,arch,origin,size
    let mut packages: Vec<PackageInfo> = tab_rows(text)
        .filter(|f| f.len() >= 8)
        .map(|f| PackageInfo {
            name: f[2].to_owned(),
            description: format!("{} — {}", f[0], f[1]),
            installed_version: if f[3].is_empty() { f[4] } else { f[3] }.to_owned(),
            architecture: f[5].to_owned(),
            repository: f[6].to_owned(),
            size: f[7].to_owned(),
            installed: true,
            ..Default::default()
        })
        .collect();
    packages.sort_by(|a, b| a.name.cmp(&b.name));
    packages
}

fn search_row(f: &[&str]) -> PackageInfo {
    // --columns=name,description,application,version,branch,remotes
    PackageInfo {
        name: f[2].to_owned(),
        description: format!("{} — {}", f[0], f[1]),
        available_version: if f[3].is_empty() { f[4] } else { f[3] }.to_owned(),
        repository: f[5].split(',').next().unwrap_or("").to_owned(),
        ..Default::default()
    }
}

const SEARCH_COLUMNS: &str = "--columns=name,description,application,version,branch,remotes";

impl FlatpakBackend {
    /// Which remote an app ID installs from.
    fn remote_for(app_id: &str) -> String {
        let out = run("flatpak", &["search", app_id, "--columns=application,remotes"], process::SHORT);
        tab_rows(&out.stdout)
            .find(|f| f.len() >= 2 && f[0] == app_id)
            .and_then(|f| f[1].split(',').next().map(str::to_owned))
            .unwrap_or_else(|| "flathub".into())
    }

    fn by_remote(names: &[String]) -> BTreeMap<String, Vec<String>> {
        let mut map: BTreeMap<String, Vec<String>> = BTreeMap::new();
        for name in names {
            map.entry(Self::remote_for(name)).or_default().push(name.clone());
        }
        map
    }

    /// The runtime an app on `remote` needs ("org.kde.Platform/x86_64/6.8").
    fn runtime_of(remote: &str, app_id: &str) -> Option<String> {
        let out = run("flatpak", &["remote-info", remote, app_id], process::SHORT);
        out.stdout.lines().find_map(|l| l.trim().strip_prefix("Runtime:").map(|r| r.trim().to_owned())).filter(|r| !r.is_empty())
    }

    fn user_override(app_id: &str, flags: &[String]) -> OperationResult {
        // ~/.local/share/flatpak/overrides is the user's own: no password.
        let mut args = vec!["override".to_owned(), "--user".into()];
        args.extend(flags.iter().cloned());
        args.push(app_id.to_owned());
        let out = run("flatpak", &args, process::SHORT);
        OperationResult { success: out.success(), output: out.combined() }
    }

    pub fn permissions(app_id: &str) -> Permissions {
        parse_permissions(&run("flatpak", &["info", "--show-permissions", app_id], process::SHORT).stdout)
    }

    pub fn set_shared(app_id: &str, name: &str, enabled: bool) -> OperationResult {
        Self::user_override(app_id, &[format!("--{}={name}", if enabled { "share" } else { "unshare" })])
    }

    pub fn set_socket(app_id: &str, name: &str, enabled: bool) -> OperationResult {
        Self::user_override(app_id, &[format!("--{}={name}", if enabled { "socket" } else { "nosocket" })])
    }

    pub fn set_device(app_id: &str, name: &str, enabled: bool) -> OperationResult {
        Self::user_override(app_id, &[format!("--{}={name}", if enabled { "device" } else { "nodevice" })])
    }

    pub fn set_feature(app_id: &str, name: &str, enabled: bool) -> OperationResult {
        Self::user_override(app_id, &[format!("--{}={name}", if enabled { "allow" } else { "disallow" })])
    }

    pub fn add_filesystem(app_id: &str, spec: &str) -> OperationResult {
        Self::user_override(app_id, &[format!("--filesystem={spec}")])
    }

    /// `--nofilesystem` takes the bare path, without a `:ro`/`:rw` suffix.
    pub fn remove_filesystem(app_id: &str, spec: &str) -> OperationResult {
        Self::user_override(app_id, &[format!("--nofilesystem={}", spec.split(':').next().unwrap_or(spec))])
    }

    pub fn set_env(app_id: &str, key: &str, value: &str) -> OperationResult {
        Self::user_override(app_id, &[format!("--env={key}={value}")])
    }

    pub fn unset_env(app_id: &str, key: &str) -> OperationResult {
        Self::user_override(app_id, &[format!("--unset-env={key}")])
    }

    /// `bus` is "session" or "system"; `kind` is "talk" or "own".
    pub fn grant_dbus_name(app_id: &str, bus: &str, kind: &str, name: &str) -> OperationResult {
        let prefix = if bus == "system" { "--system-" } else { "--" };
        Self::user_override(app_id, &[format!("{prefix}{kind}-name={name}")])
    }

    pub fn revoke_dbus_name(app_id: &str, bus: &str, kind: &str, name: &str) -> OperationResult {
        // There's --no-talk-name but no --no-own-name; owned names go
        // through the generic policy flag.
        if kind == "own" {
            return Self::user_override(app_id, &[format!("--remove-policy={bus}-bus.{name}=own")]);
        }
        let prefix = if bus == "system" { "--system-no-" } else { "--no-" };
        Self::user_override(app_id, &[format!("{prefix}{kind}-name={name}")])
    }

    pub fn reset_overrides(app_id: &str) -> OperationResult {
        Self::user_override(app_id, &["--reset".to_owned()])
    }

    /// ~/.var/app, where Flatpak apps keep their data.
    pub fn user_data_root() -> PathBuf {
        home().join(".var/app")
    }

    /// ~/.local/share/flatpak/overrides
    pub fn user_overrides_root() -> PathBuf {
        home().join(".local/share/flatpak/overrides")
    }
}

fn home() -> PathBuf {
    std::env::var_os("HOME").map(PathBuf::from).unwrap_or_default()
}

impl Backend for FlatpakBackend {
    fn key(&self) -> &'static str {
        "flatpak"
    }

    fn name(&self) -> &'static str {
        "Flatpak"
    }

    fn is_available(&self) -> bool {
        executable_exists("flatpak")
    }

    fn list_installed(&self) -> Vec<PackageInfo> {
        installed_rows(
            &run("flatpak", &["list", "--app", "--columns=name,description,application,version,branch,arch,origin,size"], process::SHORT)
                .stdout,
        )
    }

    fn search(&self, query: &str) -> Vec<PackageInfo> {
        if query.trim().is_empty() {
            return Vec::new();
        }
        let out = run("flatpak", &["search", query, SEARCH_COLUMNS], process::SHORT);
        let installed = by_name(self.list_installed());
        tab_rows(&out.stdout)
            .filter(|f| f.len() >= 6)
            .map(|f| {
                let mut pkg = search_row(&f);
                if let Some(i) = installed.get(&pkg.name) {
                    pkg.installed = true;
                    pkg.installed_version = i.installed_version.clone();
                    pkg.architecture = i.architecture.clone();
                }
                pkg
            })
            .collect()
    }

    fn package_details(&self, names: &[String]) -> Vec<PackageInfo> {
        let installed = by_name(self.list_installed());
        names
            .iter()
            .filter_map(|name| {
                if let Some(i) = installed.get(name) {
                    return Some(i.clone());
                }
                // No bulk info command for apps that aren't installed: an
                // exact match from a search.
                let out = run("flatpak", &["search", name, SEARCH_COLUMNS], process::SHORT);
                tab_rows(&out.stdout).find(|f| f.len() >= 6 && f[2] == name).map(|f| search_row(&f))
            })
            .collect()
    }

    fn install_commands(&self, names: &[String]) -> Vec<Command> {
        Self::by_remote(names)
            .into_iter()
            .map(|(remote, ids)| Command::new("flatpak", with_names(&["install", "-y", "--noninteractive", &remote], &ids)))
            .collect()
    }

    fn remove_commands(&self, names: &[String]) -> Vec<Command> {
        // A plain uninstall leaves runtimes only these apps used; sweep them too.
        vec![
            Command::new("flatpak", with_names(&["uninstall", "-y", "--noninteractive"], names)),
            Command::new("flatpak", ["uninstall", "--unused", "-y", "--noninteractive"]),
        ]
    }

    fn reinstall_commands(&self, names: &[String]) -> Vec<Command> {
        // No reinstall verb: uninstall, then install. ~/.var/app is kept.
        let mut commands = vec![Command::new("flatpak", with_names(&["uninstall", "-y", "--noninteractive"], names))];
        commands.extend(self.install_commands(names));
        commands
    }

    fn list_updates(&self) -> Vec<PackageInfo> {
        let out = run("flatpak", &["remote-ls", "--updates", "--columns=application,version,branch"], process::SHORT);
        let installed = by_name(self.list_installed());
        tab_rows(&out.stdout)
            .filter(|f| f.len() >= 3)
            .map(|f| {
                let mut pkg = PackageInfo {
                    name: f[0].to_owned(),
                    available_version: if f[1].is_empty() { f[2] } else { f[1] }.to_owned(),
                    installed: true,
                    ..Default::default()
                };
                if let Some(i) = installed.get(&pkg.name) {
                    pkg.installed_version = i.installed_version.clone();
                    pkg.description = i.description.clone();
                    pkg.repository = i.repository.clone();
                }
                pkg
            })
            .collect()
    }

    fn upgrade_commands(&self, names: &[String]) -> Vec<Command> {
        vec![Command::new("flatpak", with_names(&["update", "-y", "--noninteractive"], names))]
    }

    fn clean_unused_commands(&self) -> Vec<Command> {
        vec![Command::new("flatpak", ["uninstall", "--unused", "-y", "--noninteractive"])]
    }

    fn download_commands(&self, names: &[String], _destination: &str, include_dependencies: bool) -> Vec<Command> {
        // --no-deploy fetches into flatpak's own store (a later install is
        // instant and offline); there's no destination folder to give it.
        let mut commands = Vec::new();
        let mut runtimes = HashSet::new();
        for (remote, ids) in Self::by_remote(names) {
            commands.push(Command::new("flatpak", with_names(&["install", "--no-deploy", "-y", "--noninteractive", &remote], &ids)));
            if !include_dependencies {
                continue;
            }
            for id in &ids {
                if let Some(runtime) = Self::runtime_of(&remote, id).filter(|r| runtimes.insert(r.clone())) {
                    commands.push(Command::new("flatpak", ["install", "--no-deploy", "-y", "--noninteractive", &remote, &runtime]));
                }
            }
        }
        commands
    }

    fn preview_install(&self, names: &[String]) -> TransactionPreview {
        let installed_refs: HashSet<String> = tab_rows(&run("flatpak", &["list", "--columns=application,arch,branch"], process::SHORT).stdout)
            .filter(|f| f.len() >= 3)
            .map(|f| format!("{}/{}/{}", f[0], f[1], f[2]))
            .collect();
        let mut installs = Vec::new();
        let mut runtimes: Vec<String> = Vec::new();
        for id in names {
            let remote = Self::remote_for(id);
            installs.push(format!("  {id} (from {remote})"));
            if let Some(runtime) = Self::runtime_of(&remote, id) {
                if !installed_refs.contains(&runtime) && !runtimes.contains(&runtime) {
                    runtimes.push(runtime);
                }
            }
        }
        let mut plan = vec!["Installing:".to_owned()];
        plan.extend(installs);
        if !runtimes.is_empty() {
            plan.push(String::new());
            plan.push("Also installing runtime(s):".into());
            plan.extend(indented(runtimes.iter().map(String::as_str)));
        }
        TransactionPreview { available: true, plan_text: plan.join("\n") }
    }

    fn preview_remove(&self, names: &[String]) -> TransactionPreview {
        let runtime_by_app: HashMap<String, String> =
            tab_rows(&run("flatpak", &["list", "--app", "--columns=application,runtime"], process::SHORT).stdout)
                .filter(|f| f.len() >= 2)
                .map(|f| (f[0].to_owned(), f[1].to_owned()))
                .collect();
        let still_used: HashSet<&String> = runtime_by_app.iter().filter(|(app, _)| !names.contains(app)).map(|(_, r)| r).collect();
        let mut orphaned: Vec<String> = Vec::new();
        for id in names {
            if let Some(runtime) = runtime_by_app.get(id) {
                if !still_used.contains(runtime) && !orphaned.contains(runtime) {
                    orphaned.push(runtime.clone());
                }
            }
        }
        let mut plan = vec!["Removing:".to_owned()];
        plan.extend(indented(names.iter().map(String::as_str)));
        if !orphaned.is_empty() {
            plan.push(String::new());
            plan.push("Also removing (no longer used):".into());
            plan.extend(indented(orphaned.iter().map(String::as_str)));
        }
        TransactionPreview { available: true, plan_text: plan.join("\n") }
    }

    fn refresh_metadata_commands(&self) -> Vec<Command> {
        vec![Command::new("flatpak", ["update", "--appstream"])]
    }

    fn recent_history(&self) -> String {
        run("flatpak", &["history", "--reverse", "--columns=time,change,application,branch,remote"], process::SHORT).stdout.trim().to_owned()
    }

    fn list_repositories(&self) -> Vec<RepositoryInfo> {
        let out = run("flatpak", &["remotes", "--show-disabled", "--columns=name,title,url,options"], process::SHORT);
        tab_rows(&out.stdout)
            .filter(|f| f.len() >= 4)
            .map(|f| RepositoryInfo {
                id: f[0].to_owned(),
                name: if !f[1].is_empty() && f[1] != "-" { f[1] } else { f[0] }.to_owned(),
                url: f[2].to_owned(),
                enabled: !f[3].split(',').any(|o| o == "disabled"),
            })
            .collect()
    }

    fn set_repository_enabled(&self, id: &str, enabled: bool) -> OperationResult {
        let out = run("flatpak", &["remote-modify", if enabled { "--enable" } else { "--disable" }, id], process::SHORT);
        OperationResult { success: out.success(), output: out.combined() }
    }

    fn repository_add_fields(&self) -> Vec<RepositoryAddField> {
        vec![
            RepositoryAddField { key: "name", label: "Remote name", placeholder: "myremote", required: true },
            RepositoryAddField { key: "url", label: "Remote URL", placeholder: "https://example.com/repo/repo.flatpakrepo", required: true },
        ]
    }

    fn add_repository(&self, values: &[(String, String)]) -> OperationResult {
        let (name, url) = (field(values, "name"), field(values, "url"));
        if name.is_empty() || url.is_empty() {
            return OperationResult::failed("Remote name and URL are required");
        }
        let out = run("flatpak", &["remote-add", "--if-not-exists", name, url], process::SHORT);
        OperationResult { success: out.success(), output: out.combined() }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn installed_list() {
        let rows = installed_rows("Firefox\tFast, Private & Safe Web Browser\torg.mozilla.firefox\t131.0\tstable\tx86_64\tflathub\t280.1 MB\n");
        assert_eq!(rows[0].name, "org.mozilla.firefox");
        assert_eq!(rows[0].description, "Firefox — Fast, Private & Safe Web Browser");
        assert_eq!(rows[0].size, "280.1 MB");
    }

    #[test]
    fn permissions() {
        let text = "[Context]\nshared=network;ipc;\nsockets=x11;wayland;pulseaudio;\ndevices=dri;\nfilesystems=xdg-download;home:ro;\n\n[Session Bus Policy]\norg.freedesktop.Notifications=talk\norg.mpris.MediaPlayer2.firefox=own\n\n[Environment]\nMOZ_ENABLE_WAYLAND=1\n";
        let p = parse_permissions(text);
        assert!(p.shared.contains("network") && p.sockets.contains("wayland"));
        assert_eq!(p.filesystems, ["xdg-download", "home:ro"]);
        assert_eq!(p.session_bus_talk, ["org.freedesktop.Notifications"]);
        assert_eq!(p.session_bus_own, ["org.mpris.MediaPlayer2.firefox"]);
        assert_eq!(p.env_vars, [("MOZ_ENABLE_WAYLAND".to_owned(), "1".to_owned())]);
    }
}
