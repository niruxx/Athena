//! pacman (Arch Linux and derivatives).

use std::collections::{HashMap, HashSet};
use std::sync::LazyLock;

use regex::Regex;

use super::{indented, log_tail, read_lines, write_file_as_root};
use crate::backend::{field, Backend};
use crate::package::{OperationResult, PackageGroupInfo, PackageInfo, RepositoryAddField, RepositoryInfo, TransactionPreview};
use crate::process::{self, executable_exists, lines, run, Command};

const PACMAN_CONF: &str = "/etc/pacman.conf";

pub struct PacmanBackend;

/// `pacman -Qi`/`-Si` output: one blank-line-separated "Key : Value" block
/// per package, by package name.
pub(crate) fn parse_info_blocks(text: &str) -> HashMap<String, HashMap<String, String>> {
    let mut blocks = HashMap::new();
    let mut fields: HashMap<String, String> = HashMap::new();
    let mut flush = |fields: &mut HashMap<String, String>| {
        if let Some(name) = fields.get("Name").cloned() {
            blocks.insert(name, std::mem::take(fields));
        }
        fields.clear();
    };
    for line in text.lines() {
        if line.trim().is_empty() {
            flush(&mut fields);
            continue;
        }
        if let Some((key, value)) = line.split_once(':') {
            fields.insert(key.trim().to_owned(), value.trim().to_owned());
        }
    }
    flush(&mut fields);
    blocks
}

/// `pacman -Ss`: "repo/name version [(groups)] [installed]" headers, each
/// followed by an indented description line.
pub(crate) fn parse_search(text: &str) -> Vec<PackageInfo> {
    static HEADER: LazyLock<Regex> =
        LazyLock::new(|| Regex::new(r"^(\S+)/(\S+)\s+(\S+)(?:\s+\(([^)]*)\))?(\s+\[installed[^\]]*\])?$").unwrap());
    let mut results: Vec<PackageInfo> = Vec::new();
    for line in text.lines() {
        if line.starts_with(' ') || line.starts_with('\t') {
            if let Some(last) = results.last_mut() {
                last.description = line.trim().to_owned();
            }
            continue;
        }
        let Some(m) = HEADER.captures(line) else { continue };
        let installed = m.get(5).is_some_and(|g| !g.as_str().is_empty());
        results.push(PackageInfo {
            repository: m[1].to_owned(),
            name: m[2].to_owned(),
            available_version: m[3].to_owned(),
            installed_version: if installed { m[3].to_owned() } else { String::new() },
            installed,
            ..Default::default()
        });
    }
    results
}

/// `pacman -Qu`: "name old -> new".
pub(crate) fn parse_updates(text: &str) -> Vec<PackageInfo> {
    static ROW: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^(\S+)\s+(\S+)\s+->\s+(\S+)").unwrap());
    lines(text)
        .filter_map(|l| ROW.captures(l))
        .map(|m| PackageInfo {
            name: m[1].to_owned(),
            installed_version: m[2].to_owned(),
            available_version: m[3].to_owned(),
            installed: true,
            repository: "installed".into(),
            ..Default::default()
        })
        .collect()
}

/// The repositories in pacman.conf: every `[section]` but `[options]`,
/// enabled unless its header is commented out.
pub(crate) fn parse_repositories(lines: &[String]) -> Vec<RepositoryInfo> {
    static HEADER: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^(#)?\s*\[([^\]]+)\]\s*$").unwrap());
    static URL: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^#?\s*(?:Include|Server)\s*=\s*(.*)$").unwrap());
    let mut repos = Vec::new();
    for (i, line) in lines.iter().enumerate() {
        let Some(m) = HEADER.captures(line.trim()) else { continue };
        let name = &m[2];
        if name.eq_ignore_ascii_case("options") {
            continue;
        }
        let url = lines[i + 1..]
            .iter()
            .map(|l| l.trim())
            .take_while(|l| !l.starts_with('['))
            .find_map(|l| URL.captures(l).map(|u| u[1].trim().to_owned()))
            .unwrap_or_default();
        repos.push(RepositoryInfo { id: i.to_string(), name: name.to_owned(), url, enabled: m.get(1).is_none() });
    }
    repos
}

/// Comments a repository section in or out: its header and its own
/// directives, leaving unrelated comments alone.
pub(crate) fn toggle_repository(lines: &mut [String], header_index: usize, enabled: bool) -> bool {
    static DIRECTIVE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^#?\s*(Include|Server|SigLevel|Usage)\b").unwrap());
    if header_index >= lines.len() {
        return false;
    }
    let end = lines[header_index + 1..].iter().position(|l| l.trim().starts_with('[')).map_or(lines.len(), |p| header_index + 1 + p);
    for (i, line) in lines.iter_mut().enumerate().take(end).skip(header_index) {
        let trimmed = line.trim().to_owned();
        if i != header_index && !DIRECTIVE.is_match(&trimmed) {
            continue;
        }
        let commented = trimmed.starts_with('#');
        if enabled && commented {
            let hash = line.find('#').unwrap();
            line.remove(hash);
            if line[hash..].starts_with(' ') {
                line.remove(hash);
            }
        } else if !enabled && !commented {
            let first = line.find(|c: char| !c.is_whitespace()).unwrap_or(0);
            line.insert(first, '#');
        }
    }
    true
}

impl PacmanBackend {
    fn plan(args: &[&str], names: &[String], heading: &str) -> TransactionPreview {
        let mut all: Vec<String> = args.iter().map(|s| (*s).to_owned()).collect();
        all.extend(names.iter().cloned());
        let out = run("pacman", &all, process::LONG);
        let lines = indented(lines(&out.stdout));
        TransactionPreview {
            available: out.started,
            plan_text: if lines.is_empty() { String::new() } else { format!("{heading}\n{}", lines.join("\n")) },
        }
    }
}

impl Backend for PacmanBackend {
    fn key(&self) -> &'static str {
        "pacman"
    }

    fn name(&self) -> &'static str {
        "Pacman (Arch Linux)"
    }

    fn is_available(&self) -> bool {
        executable_exists("pacman")
    }

    fn list_installed(&self) -> Vec<PackageInfo> {
        let out = run("pacman", &["-Q"], process::SHORT);
        let mut packages: Vec<PackageInfo> = lines(&out.stdout)
            .filter_map(|l| {
                let mut fields = l.split_whitespace();
                Some(PackageInfo {
                    name: fields.next()?.to_owned(),
                    installed_version: fields.next()?.to_owned(),
                    repository: "installed".into(),
                    installed: true,
                    ..Default::default()
                })
            })
            .collect();
        packages.sort_by(|a, b| a.name.cmp(&b.name));
        packages
    }

    fn search(&self, query: &str) -> Vec<PackageInfo> {
        parse_search(&run("pacman", &["-Ss", query], process::LONG).stdout)
    }

    fn dependency_query(&self, capability: &str, find_requires: bool) -> Vec<PackageInfo> {
        let capability = capability.trim();
        // pacman can't list providers across the sync database without an
        // interactive prompt, and "Required By" only exists for installed
        // packages, so only that direction for installed packages.
        if capability.is_empty() || !find_requires {
            return Vec::new();
        }
        let blocks = parse_info_blocks(&run("pacman", &["-Qi", capability], process::SHORT).stdout);
        let required_by = blocks.get(capability).and_then(|f| f.get("Required By")).map(|s| s.trim()).unwrap_or("");
        if required_by.is_empty() || required_by.eq_ignore_ascii_case("none") {
            return Vec::new();
        }
        self.package_details(&required_by.split_whitespace().map(str::to_owned).collect::<Vec<_>>())
    }

    fn package_details(&self, names: &[String]) -> Vec<PackageInfo> {
        if names.is_empty() {
            return Vec::new();
        }
        let with = |flag: &str| std::iter::once(flag.to_owned()).chain(names.iter().cloned()).collect::<Vec<_>>();
        let local = parse_info_blocks(&run("pacman", &with("-Qi"), process::SHORT).stdout);
        let sync = parse_info_blocks(&run("pacman", &with("-Si"), process::SHORT).stdout);
        let url = |fields: &HashMap<String, String>| fields.get("URL").filter(|u| *u != "None").cloned().unwrap_or_default();
        names
            .iter()
            .filter(|n| local.contains_key(*n) || sync.contains_key(*n))
            .map(|name| {
                let mut pkg = PackageInfo { name: name.clone(), ..Default::default() };
                if let Some(f) = local.get(name) {
                    pkg.installed_version = f.get("Version").cloned().unwrap_or_default();
                    pkg.installed = true;
                    pkg.architecture = f.get("Architecture").cloned().unwrap_or_default();
                    pkg.description = f.get("Description").cloned().unwrap_or_default();
                    pkg.long_description = pkg.description.clone();
                    pkg.repository = "installed".into();
                    pkg.homepage_url = url(f);
                    pkg.size = f.get("Installed Size").cloned().unwrap_or_default();
                }
                if let Some(f) = sync.get(name) {
                    pkg.available_version = f.get("Version").cloned().unwrap_or_default();
                    if pkg.architecture.is_empty() {
                        pkg.architecture = f.get("Architecture").cloned().unwrap_or_default();
                    }
                    if pkg.description.is_empty() {
                        pkg.description = f.get("Description").cloned().unwrap_or_default();
                        pkg.long_description = pkg.description.clone();
                    }
                    if !pkg.installed {
                        pkg.repository = f.get("Repository").cloned().unwrap_or_default();
                    }
                    if pkg.homepage_url.is_empty() {
                        pkg.homepage_url = url(f);
                    }
                    if pkg.size.is_empty() {
                        pkg.size = f.get("Download Size").cloned().unwrap_or_default();
                    }
                }
                pkg
            })
            .collect()
    }

    fn list_groups(&self) -> Vec<PackageGroupInfo> {
        // Bare -Sg prints one group per line; bare -Qg prints "group package"
        // pairs, so one call covers every installed group.
        let all = run("pacman", &["-Sg"], process::SHORT);
        let installed: HashSet<String> = lines(&run("pacman", &["-Qg"], process::SHORT).stdout)
            .filter_map(|l| l.split_whitespace().next().map(str::to_owned))
            .collect();
        lines(&all.stdout)
            .map(str::trim)
            .map(|g| PackageGroupInfo { id: g.to_owned(), name: g.to_owned(), installed: installed.contains(g), ..Default::default() })
            .collect()
    }

    fn group_details(&self, id: &str, _is_meta: bool) -> PackageGroupInfo {
        let packages = lines(&run("pacman", &["-Sg", id], process::SHORT).stdout)
            .filter_map(|l| l.split_whitespace().nth(1).map(str::to_owned))
            .collect();
        let check = run("pacman", &["-Qg", id], process::SHORT);
        PackageGroupInfo {
            id: id.to_owned(),
            name: id.to_owned(),
            installed: check.exit_code == 0 && !check.stdout.trim().is_empty(),
            packages,
            ..Default::default()
        }
    }

    fn install_commands(&self, names: &[String]) -> Vec<Command> {
        vec![Command::privileged(["pacman", "-S", "--noconfirm"].iter().map(|s| s.to_string()).chain(names.iter().cloned()))]
    }

    fn remove_commands(&self, names: &[String]) -> Vec<Command> {
        // -Rs: also the dependencies they pulled in that nothing else needs.
        vec![Command::privileged(["pacman", "-Rs", "--noconfirm"].iter().map(|s| s.to_string()).chain(names.iter().cloned()))]
    }

    fn reinstall_commands(&self, names: &[String]) -> Vec<Command> {
        // Plain -S reinstalls a named installed package (only --needed skips it).
        self.install_commands(names)
    }

    fn list_updates(&self) -> Vec<PackageInfo> {
        parse_updates(&run("pacman", &["-Qu"], process::SHORT).stdout)
    }

    fn upgrade_commands(&self, names: &[String]) -> Vec<Command> {
        // Everything is a full -Syu: Arch doesn't support partial upgrades.
        if names.is_empty() {
            return vec![Command::privileged(["pacman", "-Syu", "--noconfirm"])];
        }
        vec![Command::privileged(["pacman", "-S", "--needed", "--noconfirm"].iter().map(|s| s.to_string()).chain(names.iter().cloned()))]
    }

    fn clean_unused_commands(&self) -> Vec<Command> {
        // No single autoremove: list orphaned dependencies, then remove them.
        let orphans: Vec<String> = lines(&run("pacman", &["-Qdtq"], process::SHORT).stdout).map(str::to_owned).collect();
        if orphans.is_empty() {
            return Vec::new();
        }
        vec![Command::privileged(["pacman", "-R", "--noconfirm"].iter().map(|s| s.to_string()).chain(orphans))]
    }

    fn download_commands(&self, names: &[String], destination: &str, include_dependencies: bool) -> Vec<Command> {
        // -Sw downloads dependencies too unless --nodeps. It still takes
        // pacman's database lock, hence pkexec.
        let mut args = vec!["pacman".to_owned(), "-Sw".into(), "--noconfirm".into()];
        if !include_dependencies {
            args.push("--nodeps".into());
        }
        if !destination.is_empty() {
            args.push(format!("--cachedir={destination}"));
        }
        args.extend(names.iter().cloned());
        vec![Command::privileged(args)]
    }

    fn preview_install(&self, names: &[String]) -> TransactionPreview {
        Self::plan(&["-S", "--print", "--print-format", "%n %v"], names, "Installing (including dependencies):")
    }

    fn preview_remove(&self, names: &[String]) -> TransactionPreview {
        Self::plan(&["-Rs", "--print", "--print-format", "%n"], names, "Removing (including now-unused dependencies):")
    }

    fn refresh_metadata_commands(&self) -> Vec<Command> {
        vec![Command::privileged(["pacman", "-Sy", "--noconfirm"])]
    }

    fn recent_history(&self) -> String {
        log_tail("/var/log/pacman.log", 500).unwrap_or_else(|| "No history log found (expected at /var/log/pacman.log).".into())
    }

    fn list_repositories(&self) -> Vec<RepositoryInfo> {
        parse_repositories(&read_lines(PACMAN_CONF))
    }

    fn set_repository_enabled(&self, id: &str, enabled: bool) -> OperationResult {
        let mut lines = read_lines(PACMAN_CONF);
        let found = id.parse::<usize>().is_ok_and(|index| toggle_repository(&mut lines, index, enabled));
        if !found {
            return OperationResult::failed(format!("Repository entry no longer found in {PACMAN_CONF}"));
        }
        if write_file_as_root(PACMAN_CONF, &lines) {
            OperationResult { success: true, output: String::new() }
        } else {
            OperationResult::failed(format!("Failed to write {PACMAN_CONF} (are you authorized for admin actions?)"))
        }
    }

    fn repository_add_fields(&self) -> Vec<RepositoryAddField> {
        vec![
            RepositoryAddField { key: "name", label: "Repository name", placeholder: "myrepo", required: true },
            RepositoryAddField { key: "server", label: "Server URL", placeholder: "https://example.com/repo/$arch", required: true },
        ]
    }

    fn add_repository(&self, values: &[(String, String)]) -> OperationResult {
        let (name, server) = (field(values, "name"), field(values, "server"));
        if name.is_empty() || server.is_empty() {
            return OperationResult::failed("Repository name and server URL are required");
        }
        let mut lines = read_lines(PACMAN_CONF);
        if lines.is_empty() {
            return OperationResult::failed(format!("Could not read {PACMAN_CONF}"));
        }
        lines.extend([String::new(), format!("[{name}]"), format!("Server = {server}")]);
        if write_file_as_root(PACMAN_CONF, &lines) {
            OperationResult { success: true, output: String::new() }
        } else {
            OperationResult::failed(format!("Failed to write {PACMAN_CONF} (are you authorized for admin actions?)"))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn search_output() {
        let text = "core/linux 6.17.1.arch1-1 [installed]\n    The Linux kernel and modules\nextra/linux-docs 6.17.1.arch1-1 (docs)\n    Documentation\n";
        let results = parse_search(text);
        assert_eq!(results.len(), 2);
        assert!(results[0].installed);
        assert_eq!(results[0].installed_version, "6.17.1.arch1-1");
        assert_eq!(results[0].description, "The Linux kernel and modules");
        assert_eq!(results[1].repository, "extra");
        assert!(!results[1].installed);
    }

    #[test]
    fn info_blocks() {
        let text = "Name            : firefox\nVersion         : 131.0-1\nURL             : https://www.mozilla.org\n\nName            : zstd\nVersion         : 1.5\nURL             : None\n";
        let blocks = parse_info_blocks(text);
        assert_eq!(blocks["firefox"]["Version"], "131.0-1");
        assert_eq!(blocks["zstd"]["URL"], "None");
    }

    #[test]
    fn updates_output() {
        let u = parse_updates("mesa 1:25.2.4-1 -> 1:25.2.5-1\nlinux 6.17-1 -> 6.17.2-1 [ignored]\n");
        assert_eq!(u.len(), 2);
        assert_eq!(u[0].available_version, "1:25.2.5-1");
    }

    #[test]
    fn repositories_round_trip() {
        let mut conf: Vec<String> = "[options]\nHoldPkg = pacman\n\n#[multilib]\n#Include = /etc/pacman.d/mirrorlist\n\n[core]\nInclude = /etc/pacman.d/mirrorlist\n# a comment\n"
            .lines()
            .map(str::to_owned)
            .collect();
        let repos = parse_repositories(&conf);
        assert_eq!(repos.len(), 2);
        assert_eq!((repos[0].name.as_str(), repos[0].enabled), ("multilib", false));
        assert_eq!(repos[1].url, "/etc/pacman.d/mirrorlist");
        assert!(toggle_repository(&mut conf, repos[0].id.parse().unwrap(), true));
        assert_eq!(conf[3], "[multilib]");
        assert_eq!(conf[4], "Include = /etc/pacman.d/mirrorlist");
        assert!(toggle_repository(&mut conf, 6, false));
        assert_eq!(conf[6], "#[core]");
        assert_eq!(conf[8], "# a comment", "unrelated comments stay");
    }
}
