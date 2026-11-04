//! APT (Debian, Ubuntu, and derivatives).

use std::collections::HashMap;
use std::sync::LazyLock;

use regex::Regex;

use super::{by_name, human_size, log_tail, read_lines, with_names, write_file_as_root};
use crate::backend::{field, Backend};
use crate::package::{OperationResult, PackageGroupInfo, PackageInfo, RepositoryAddField, RepositoryInfo, TransactionPreview};
use crate::process::{self, executable_exists, lines, run, Command};

pub struct AptBackend;

/// Separates the parts of a repository ID; won't appear in paths or text.
const ID_SEP: char = '\u{1f}';

/// apt-get through pkexec, non-interactive (debconf prompts would hang:
/// there's no terminal behind pkexec), with machine-readable progress lines
/// (`pmstatus:pkg:42.5:…`) the terminal window turns into a progress bar.
fn apt_get(args: &[&str], names: &[String]) -> Command {
    let base = ["env", "DEBIAN_FRONTEND=noninteractive", "apt-get", "-o", "APT::Status-Fd=1"];
    Command::privileged(base.iter().chain(args).map(|s| (*s).to_owned()).chain(names.iter().cloned()))
}

#[derive(Debug, Default, Clone, PartialEq, Eq)]
pub(crate) struct Versions {
    installed: String,
    candidate: String,
}

/// `apt-cache policy <names>`: "name:" headers with indented Installed: and
/// Candidate: lines.
pub(crate) fn parse_policy(text: &str) -> HashMap<String, Versions> {
    let mut result: HashMap<String, Versions> = HashMap::new();
    let mut current = String::new();
    for line in text.lines() {
        if !line.is_empty() && !line.starts_with(' ') && line.ends_with(':') {
            current = line[..line.len() - 1].trim().to_owned();
            continue;
        }
        if current.is_empty() {
            continue;
        }
        let t = line.trim();
        let value = |prefix: &str| t.strip_prefix(prefix).map(str::trim).filter(|v| *v != "(none)").map(str::to_owned);
        if let Some(v) = value("Installed:") {
            result.entry(current.clone()).or_default().installed = v;
        } else if let Some(v) = value("Candidate:") {
            result.entry(current.clone()).or_default().candidate = v;
        }
    }
    result
}

#[derive(Debug, Default, Clone, PartialEq, Eq)]
pub(crate) struct Description {
    summary: String,
    long: String,
    homepage: String,
    size: String,
}

/// `apt-cache show <names>`: control-file stanzas; the first per package.
pub(crate) fn parse_show(text: &str) -> HashMap<String, Description> {
    let mut result = HashMap::new();
    let (mut name, mut homepage, mut size) = (String::new(), String::new(), String::new());
    let mut description: Vec<String> = Vec::new();
    let mut in_description = false;
    let mut flush = |name: &mut String, homepage: &mut String, size: &mut String, description: &mut Vec<String>| {
        if !name.is_empty() && !description.is_empty() && !result.contains_key(name.as_str()) {
            let long = description[1..].iter().map(|l| if l == "." { "" } else { l.as_str() }).collect::<Vec<_>>().join(" ");
            result.insert(
                name.clone(),
                Description {
                    summary: description[0].clone(),
                    long: long.split_whitespace().collect::<Vec<_>>().join(" "),
                    homepage: homepage.clone(),
                    size: size.clone(),
                },
            );
        }
        name.clear();
        homepage.clear();
        size.clear();
        description.clear();
    };
    for line in text.lines() {
        if line.is_empty() {
            flush(&mut name, &mut homepage, &mut size, &mut description);
            in_description = false;
        } else if let Some(v) = line.strip_prefix("Package:") {
            name = v.trim().to_owned();
            in_description = false;
        } else if let Some(v) = line.strip_prefix("Homepage:") {
            homepage = v.trim().to_owned();
            in_description = false;
        } else if let Some(v) = line.strip_prefix("Installed-Size:") {
            // In KiB.
            size = v.trim().parse::<u64>().ok().filter(|k| *k > 0).map(|k| human_size(k * 1024)).unwrap_or_default();
            in_description = false;
        } else if line.starts_with("Description:") || line.starts_with("Description-en:") {
            description = vec![line.split_once(':').map(|(_, v)| v.trim().to_owned()).unwrap_or_default()];
            in_description = true;
        } else if in_description && line.starts_with(' ') {
            description.push(line.trim().to_owned());
        } else {
            in_description = false;
        }
    }
    flush(&mut name, &mut homepage, &mut size, &mut description);
    result
}

/// `apt list --upgradable`: "name/suite new arch [upgradable from: old]".
pub(crate) fn parse_upgradable(text: &str) -> Vec<PackageInfo> {
    static ROW: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^(\S+?)/\S+\s+(\S+)\s+(\S+)\s+\[upgradable from:\s*([^\]]+)\]").unwrap());
    lines(text)
        .filter_map(|l| ROW.captures(l))
        .map(|m| PackageInfo {
            name: m[1].to_owned(),
            available_version: m[2].to_owned(),
            architecture: m[3].to_owned(),
            installed_version: m[4].trim().to_owned(),
            installed: true,
            repository: "installed".into(),
            ..Default::default()
        })
        .collect()
}

fn source_files() -> Vec<String> {
    let mut paths = Vec::new();
    if std::path::Path::new("/etc/apt/sources.list").exists() {
        paths.push("/etc/apt/sources.list".to_owned());
    }
    if let Ok(entries) = std::fs::read_dir("/etc/apt/sources.list.d") {
        let mut files: Vec<String> = entries
            .flatten()
            .map(|e| e.path().to_string_lossy().into_owned())
            .filter(|p| p.ends_with(".list") || p.ends_with(".sources"))
            .collect();
        files.sort();
        paths.extend(files);
    }
    paths
}

/// The URI in "deb [options] URI suite components…" (options may span
/// several whitespace-separated tokens).
fn classic_uri(line: &str) -> String {
    let mut in_options = false;
    for token in line.split_whitespace().skip(1) {
        if in_options {
            in_options = !token.ends_with(']');
            continue;
        }
        if token.starts_with('[') {
            in_options = !token.ends_with(']');
            continue;
        }
        return token.to_owned();
    }
    String::new()
}

static CLASSIC_ENTRY: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^(#\s*)?((?:deb|deb-src)\s+.*)$").unwrap());

/// One-line `deb` entries in sources.list and *.list files.
pub(crate) fn parse_classic(path: &str, lines: &[String]) -> Vec<RepositoryInfo> {
    lines
        .iter()
        .enumerate()
        .filter_map(|(i, line)| {
            let m = CLASSIC_ENTRY.captures(line.trim())?;
            let active = m[2].split_whitespace().collect::<Vec<_>>().join(" ");
            Some(RepositoryInfo { id: format!("L{ID_SEP}{path}{ID_SEP}{i}"), url: classic_uri(&active), name: active, enabled: m.get(1).is_none() })
        })
        .collect()
}

/// Deb822 *.sources stanzas; enabled unless "Enabled: no".
pub(crate) fn parse_deb822(path: &str, lines: &[String]) -> Vec<RepositoryInfo> {
    let mut repos = Vec::new();
    let mut start: Option<usize> = None;
    let mut fields: HashMap<String, String> = HashMap::new();
    let mut flush = |start: &mut Option<usize>, fields: &mut HashMap<String, String>| {
        if let (Some(s), true) = (*start, fields.contains_key("Types")) {
            let uris = fields.get("URIs").cloned().unwrap_or_default();
            repos.push(RepositoryInfo {
                id: format!("S{ID_SEP}{path}{ID_SEP}{s}"),
                name: fields.get("Suites").cloned().unwrap_or_else(|| uris.clone()),
                url: uris,
                enabled: !fields.get("Enabled").is_some_and(|v| v.eq_ignore_ascii_case("no")),
            });
        }
        *start = None;
        fields.clear();
    };
    for (i, line) in lines.iter().enumerate() {
        if line.trim().is_empty() {
            flush(&mut start, &mut fields);
            continue;
        }
        start.get_or_insert(i);
        if let Some((k, v)) = line.split_once(':') {
            fields.insert(k.trim().to_owned(), v.trim().to_owned());
        }
    }
    flush(&mut start, &mut fields);
    repos
}

/// Comments a classic `deb` line in or out.
pub(crate) fn toggle_classic(lines: &mut [String], index: usize, enabled: bool) -> bool {
    let Some(line) = lines.get(index) else { return false };
    let Some(m) = CLASSIC_ENTRY.captures(line.trim()) else { return false };
    let entry = m[2].to_owned();
    lines[index] = if enabled { entry } else { format!("# {entry}") };
    true
}

/// Sets "Enabled:" in a deb822 stanza (adding it after the stanza's first line).
pub(crate) fn toggle_deb822(lines: &mut Vec<String>, start: usize, enabled: bool) -> bool {
    if start >= lines.len() {
        return false;
    }
    let end = lines[start..].iter().position(|l| l.trim().is_empty()).map_or(lines.len(), |p| start + p);
    let new_line = format!("Enabled: {}", if enabled { "yes" } else { "no" });
    match (start..end).find(|&i| lines[i].to_lowercase().starts_with("enabled:")) {
        Some(i) => lines[i] = new_line,
        None => lines.insert(start + 1, new_line),
    }
    true
}

fn plan(raw: &str) -> String {
    raw.lines().filter(|l| !l.trim().is_empty() && !l.trim().ends_with("... Done")).collect::<Vec<_>>().join("\n").trim().to_owned()
}

impl Backend for AptBackend {
    fn key(&self) -> &'static str {
        "apt"
    }

    fn name(&self) -> &'static str {
        "APT (Debian / Ubuntu)"
    }

    fn is_available(&self) -> bool {
        executable_exists("apt-cache") && executable_exists("dpkg-query")
    }

    fn list_installed(&self) -> Vec<PackageInfo> {
        let format = "${Package}\t${Version}\t${Architecture}\t${Installed-Size}\t${Homepage}\t${Status}\t${binary:Summary}\n";
        let out = run("dpkg-query", &["-W", "-f", format], process::LONG);
        let mut packages: Vec<PackageInfo> = lines(&out.stdout)
            .map(|l| l.split('\t').collect::<Vec<_>>())
            .filter(|f| f.len() >= 6 && f[5].contains("installed") && !f[5].contains("not-installed"))
            .map(|f| PackageInfo {
                name: f[0].to_owned(),
                installed_version: f[1].to_owned(),
                architecture: f[2].to_owned(),
                // Installed-Size is in KiB.
                size: f[3].parse::<u64>().ok().filter(|k| *k > 0).map(|k| human_size(k * 1024)).unwrap_or_default(),
                homepage_url: f[4].to_owned(),
                description: f.get(6).copied().unwrap_or("").to_owned(),
                repository: "installed".into(),
                installed: true,
                ..Default::default()
            })
            .collect();
        packages.sort_by(|a, b| a.name.cmp(&b.name));
        packages
    }

    fn search(&self, query: &str) -> Vec<PackageInfo> {
        static ROW: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^(\S+)\s+-\s+(.*)$").unwrap());
        let out = run("apt-cache", &["search", query], process::LONG);
        let installed = by_name(self.list_installed());
        lines(&out.stdout)
            .filter_map(|l| ROW.captures(l))
            .map(|m| {
                let mut pkg = PackageInfo { name: m[1].to_owned(), description: m[2].to_owned(), repository: "available".into(), ..Default::default() };
                if let Some(i) = installed.get(&pkg.name) {
                    pkg.installed = true;
                    pkg.installed_version = i.installed_version.clone();
                    pkg.architecture = i.architecture.clone();
                    pkg.repository = "installed".into();
                    pkg.size = i.size.clone();
                    pkg.homepage_url = i.homepage_url.clone();
                }
                pkg
            })
            .collect()
    }

    fn dependency_query(&self, capability: &str, find_requires: bool) -> Vec<PackageInfo> {
        let capability = capability.trim();
        if capability.is_empty() {
            return Vec::new();
        }
        let mut names: Vec<String> = Vec::new();
        let mut push = |name: &str| {
            if !name.is_empty() && !names.iter().any(|n| n == name) {
                names.push(name.to_owned());
            }
        };
        if find_requires {
            // `apt-cache rdepends`: indented names under "Reverse Depends:",
            // maybe with a version in parentheses or a leading "|".
            let out = run("apt-cache", &["rdepends", capability], process::SHORT);
            let mut in_section = false;
            for line in out.stdout.lines().map(str::trim).filter(|l| !l.is_empty()) {
                if line.eq_ignore_ascii_case("Reverse Depends:") {
                    in_section = true;
                } else if in_section {
                    let name = line.trim_start_matches('|');
                    push(name.split('(').next().unwrap_or(name).trim());
                }
            }
        } else {
            // `apt-cache showpkg`: "name version" pairs under "Reverse Provides:".
            let out = run("apt-cache", &["showpkg", capability], process::SHORT);
            let mut in_section = false;
            for line in out.stdout.lines().map(str::trim) {
                if line.is_empty() || (line.ends_with(':') && !line.eq_ignore_ascii_case("Reverse Provides:")) {
                    in_section = false;
                } else if line.eq_ignore_ascii_case("Reverse Provides:") {
                    in_section = true;
                } else if in_section {
                    push(line.split_whitespace().next().unwrap_or(""));
                }
            }
        }
        self.package_details(&names)
    }

    fn package_details(&self, names: &[String]) -> Vec<PackageInfo> {
        if names.is_empty() {
            return Vec::new();
        }
        let versions = parse_policy(&run("apt-cache", &with_names(&["policy"], names), process::LONG).stdout);
        let descriptions = parse_show(&run("apt-cache", &with_names(&["show"], names), process::LONG).stdout);
        names
            .iter()
            .filter(|n| versions.contains_key(*n) || descriptions.contains_key(*n))
            .map(|name| {
                let v = versions.get(name).cloned().unwrap_or_default();
                let d = descriptions.get(name).cloned().unwrap_or_default();
                let installed = !v.installed.is_empty();
                PackageInfo {
                    name: name.clone(),
                    installed_version: v.installed,
                    available_version: v.candidate,
                    installed,
                    repository: if installed { "installed" } else { "available" }.into(),
                    description: d.summary,
                    long_description: d.long,
                    homepage_url: d.homepage,
                    size: d.size,
                    ..Default::default()
                }
            })
            .collect()
    }

    fn list_groups(&self) -> Vec<PackageGroupInfo> {
        static ROW: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^([iu])\s+(\S+)\s+(.*)$").unwrap());
        if !executable_exists("tasksel") {
            return Vec::new();
        }
        lines(&run("tasksel", &["--list-tasks"], process::SHORT).stdout)
            .filter_map(|l| ROW.captures(l))
            .map(|m| PackageGroupInfo {
                id: m[2].to_owned(),
                name: m[2].to_owned(),
                description: m[3].trim().to_owned(),
                installed: &m[1] == "i",
                is_meta: true,
                ..Default::default()
            })
            .collect()
    }

    fn group_details(&self, id: &str, _is_meta: bool) -> PackageGroupInfo {
        // A task is a task-<id> metapackage; its dependencies are the task.
        static DEP: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^\s*(?:Depends|Recommends):\s*<?([A-Za-z0-9.+\-]+)>?\s*$").unwrap());
        let metapackage = format!("task-{id}");
        let mut packages: Vec<String> = Vec::new();
        for m in lines(&run("apt-cache", &["depends", &metapackage], process::SHORT).stdout).filter_map(|l| DEP.captures(l)) {
            if !packages.contains(&m[1].to_owned()) {
                packages.push(m[1].to_owned());
            }
        }
        let installed = run("dpkg-query", &["-W", "-f", "${Status}", &metapackage], process::SHORT).stdout.contains("install ok installed");
        PackageGroupInfo { id: id.to_owned(), name: id.to_owned(), is_meta: true, installed, packages, ..Default::default() }
    }

    fn install_commands(&self, names: &[String]) -> Vec<Command> {
        vec![apt_get(&["install", "-y"], names)]
    }

    fn remove_commands(&self, names: &[String]) -> Vec<Command> {
        // --auto-remove: the now-unused dependencies go in the same transaction.
        vec![apt_get(&["remove", "-y", "--auto-remove"], names)]
    }

    fn reinstall_commands(&self, names: &[String]) -> Vec<Command> {
        vec![apt_get(&["install", "-y", "--reinstall"], names)]
    }

    fn list_updates(&self) -> Vec<PackageInfo> {
        let installed = by_name(self.list_installed());
        parse_upgradable(&run("apt", &["list", "--upgradable"], process::LONG).stdout)
            .into_iter()
            .map(|mut pkg| {
                if let Some(i) = installed.get(&pkg.name) {
                    pkg.homepage_url = i.homepage_url.clone();
                    pkg.description = i.description.clone();
                }
                pkg
            })
            .collect()
    }

    fn upgrade_commands(&self, names: &[String]) -> Vec<Command> {
        if names.is_empty() {
            return vec![apt_get(&["--with-new-pkgs", "upgrade", "-y"], &[])];
        }
        vec![apt_get(&["install", "-y", "--only-upgrade"], names)]
    }

    fn clean_unused_commands(&self) -> Vec<Command> {
        vec![apt_get(&["autoremove", "-y"], &[])]
    }

    fn download_commands(&self, names: &[String], destination: &str, include_dependencies: bool) -> Vec<Command> {
        static DEP: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^\s*\|?(?:Pre)?Depends:\s*([A-Za-z0-9.+\-]+)(?:\s*\([^)]*\))?\s*$").unwrap());
        let mut all = names.to_vec();
        if include_dependencies {
            // Direct dependencies only; <virtual> names aren't downloadable.
            let args = with_names(&["depends", "--no-recommends", "--no-suggests", "--no-conflicts", "--no-breaks", "--no-replaces", "--no-enhances"], names);
            for m in lines(&run("apt-cache", &args, process::SHORT).stdout).filter_map(|l| DEP.captures(l)) {
                if !all.contains(&m[1].to_owned()) {
                    all.push(m[1].to_owned());
                }
            }
        }
        // apt-get download writes to its working directory. Unprivileged.
        let command = Command::new("apt-get", with_names(&["download"], &all));
        vec![if destination.is_empty() { command } else { command.in_dir(destination) }]
    }

    fn preview_install(&self, names: &[String]) -> TransactionPreview {
        let out = run("apt-get", &with_names(&["install", "--dry-run"], names), process::LONG);
        TransactionPreview { available: out.started, plan_text: plan(&out.stdout) }
    }

    fn preview_remove(&self, names: &[String]) -> TransactionPreview {
        let out = run("apt-get", &with_names(&["remove", "--dry-run", "--auto-remove"], names), process::LONG);
        TransactionPreview { available: out.started, plan_text: plan(&out.stdout) }
    }

    fn refresh_metadata_commands(&self) -> Vec<Command> {
        vec![Command::privileged(["env", "DEBIAN_FRONTEND=noninteractive", "apt-get", "update"])]
    }

    fn recent_history(&self) -> String {
        log_tail("/var/log/apt/history.log", 500).unwrap_or_else(|| "No history log found (expected at /var/log/apt/history.log).".into())
    }

    fn list_repositories(&self) -> Vec<RepositoryInfo> {
        source_files()
            .iter()
            .flat_map(|path| {
                let lines = read_lines(path);
                if path.ends_with(".sources") { parse_deb822(path, &lines) } else { parse_classic(path, &lines) }
            })
            .collect()
    }

    fn set_repository_enabled(&self, id: &str, enabled: bool) -> OperationResult {
        let parts: Vec<&str> = id.split(ID_SEP).collect();
        let [kind, path, index] = parts[..] else { return OperationResult::failed("Malformed repository id") };
        let Ok(index) = index.parse::<usize>() else { return OperationResult::failed("Malformed repository id") };
        let mut lines = read_lines(path);
        let found = if kind == "S" { toggle_deb822(&mut lines, index, enabled) } else { toggle_classic(&mut lines, index, enabled) };
        if !found {
            return OperationResult::failed(format!("Repository entry no longer found in {path}"));
        }
        if write_file_as_root(path, &lines) {
            OperationResult { success: true, output: String::new() }
        } else {
            OperationResult::failed(format!("Failed to write {path} (are you authorized for admin actions?)"))
        }
    }

    fn repository_add_fields(&self) -> Vec<RepositoryAddField> {
        vec![
            RepositoryAddField { key: "uri", label: "Repository URL", placeholder: "https://example.com/debian", required: true },
            RepositoryAddField { key: "suite", label: "Suite/Distribution", placeholder: "stable", required: true },
            RepositoryAddField { key: "components", label: "Components", placeholder: "main", required: true },
        ]
    }

    fn add_repository(&self, values: &[(String, String)]) -> OperationResult {
        static SCHEME: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^[a-z]+://").unwrap());
        static UNSAFE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"[^A-Za-z0-9.-]+").unwrap());
        let (uri, suite, components) = (field(values, "uri"), field(values, "suite"), field(values, "components"));
        if uri.is_empty() || suite.is_empty() || components.is_empty() {
            return OperationResult::failed("Repository URL, suite, and components are required");
        }
        // Its own file, so it's easy to find and remove later.
        let slug = UNSAFE.replace_all(&SCHEME.replace(uri, ""), "-").into_owned();
        let path = format!("/etc/apt/sources.list.d/athena-{}.list", if slug.is_empty() { "custom" } else { &slug });
        if write_file_as_root(&path, &[format!("deb {uri} {suite} {components}")]) {
            OperationResult { success: true, output: String::new() }
        } else {
            OperationResult::failed(format!("Failed to write {path} (are you authorized for admin actions?)"))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn owned(text: &str) -> Vec<String> {
        text.lines().map(str::to_owned).collect()
    }

    #[test]
    fn policy() {
        let v = parse_policy("firefox:\n  Installed: 131.0\n  Candidate: 132.0\nvlc:\n  Installed: (none)\n  Candidate: 3.0\n");
        assert_eq!(v["firefox"].candidate, "132.0");
        assert!(v["vlc"].installed.is_empty());
    }

    #[test]
    fn show() {
        let d = parse_show("Package: vlc\nInstalled-Size: 2048\nHomepage: https://videolan.org\nDescription: multimedia player\n VLC plays\n .\n everything\n\nPackage: vlc\nDescription: second stanza\n");
        assert_eq!(d["vlc"].summary, "multimedia player");
        assert_eq!(d["vlc"].long, "VLC plays everything");
        assert_eq!(d["vlc"].size, "2.0 MB");
    }

    #[test]
    fn upgradable() {
        let u = parse_upgradable("Listing...\nfirefox/noble-updates 132.0 amd64 [upgradable from: 131.0]\n");
        assert_eq!((u[0].name.as_str(), u[0].installed_version.as_str()), ("firefox", "131.0"));
    }

    #[test]
    fn classic_sources() {
        let mut lines = owned("deb [arch=amd64 signed-by=/k.gpg] http://deb.debian.org/debian trixie main\n# deb-src http://deb.debian.org/debian trixie main\n");
        let repos = parse_classic("/etc/apt/sources.list", &lines);
        assert_eq!(repos[0].url, "http://deb.debian.org/debian");
        assert!(repos[0].enabled && !repos[1].enabled);
        assert!(toggle_classic(&mut lines, 1, true));
        assert_eq!(lines[1], "deb-src http://deb.debian.org/debian trixie main");
    }

    #[test]
    fn deb822_sources() {
        let mut lines = owned("Types: deb\nURIs: http://deb.debian.org/debian\nSuites: trixie\nComponents: main\n\nTypes: deb\nURIs: http://x\nSuites: y\nEnabled: no\n");
        let repos = parse_deb822("/etc/apt/sources.list.d/debian.sources", &lines);
        assert_eq!(repos.len(), 2);
        assert!(repos[0].enabled && !repos[1].enabled);
        assert!(toggle_deb822(&mut lines, 0, false));
        assert_eq!(lines[1], "Enabled: no");
    }
}
