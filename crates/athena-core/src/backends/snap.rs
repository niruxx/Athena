//! Snap. The `snap` client has no polkit integration of its own, so
//! changes go through pkexec.

use std::collections::{HashMap, HashSet};
use std::sync::LazyLock;

use regex::Regex;

use super::{by_name, with_names};
use crate::backend::Backend;
use crate::package::PackageInfo;
use crate::process::{self, executable_exists, lines, run, Command};

pub struct SnapBackend;

/// Table rows without the "Name …" header.
fn data_lines(text: &str) -> impl Iterator<Item = &str> {
    lines(text).filter(|l| !l.starts_with("Name"))
}

/// Top-level "key: value" lines of `snap info`, plus the multi-line
/// description.
pub(crate) fn parse_info(text: &str) -> (HashMap<String, String>, String) {
    static KEY: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^(\S[\w-]*):\s?(.*)$").unwrap());
    let mut fields = HashMap::new();
    let mut description: Vec<String> = Vec::new();
    let mut in_description = false;
    for line in text.lines() {
        if line.starts_with(' ') || line.starts_with('\t') {
            if in_description {
                description.push(line.trim().to_owned());
            }
            continue;
        }
        in_description = false;
        let Some(m) = KEY.captures(line) else { continue };
        let value = m[2].trim().to_owned();
        if &m[1] == "description" {
            if value.is_empty() || value == "|" {
                in_description = true;
            } else {
                description.push(value.clone());
            }
        }
        fields.insert(m[1].to_owned(), value);
    }
    (fields, description.join("\n"))
}

/// `snap find`: Name, Version, Publisher, Notes, then a free-text summary.
pub(crate) fn parse_find(text: &str) -> Vec<PackageInfo> {
    static ROW: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(.*)$").unwrap());
    data_lines(text)
        .filter_map(|l| ROW.captures(l))
        .map(|m| PackageInfo {
            name: m[1].to_owned(),
            available_version: m[2].to_owned(),
            repository: m[3].to_owned(),
            description: m[5].to_owned(),
            ..Default::default()
        })
        .collect()
}

impl Backend for SnapBackend {
    fn key(&self) -> &'static str {
        "snap"
    }

    fn name(&self) -> &'static str {
        "Snap"
    }

    fn is_available(&self) -> bool {
        executable_exists("snap")
    }

    fn list_installed(&self) -> Vec<PackageInfo> {
        let mut packages: Vec<PackageInfo> = data_lines(&run("snap", &["list"], process::SHORT).stdout)
            .map(|l| l.split_whitespace().collect::<Vec<_>>())
            .filter(|f| f.len() >= 4)
            .map(|f| PackageInfo {
                name: f[0].to_owned(),
                installed_version: f[1].to_owned(),
                repository: f[3].to_owned(), // tracking channel
                installed: true,
                ..Default::default()
            })
            .collect();
        packages.sort_by(|a, b| a.name.cmp(&b.name));
        packages
    }

    fn search(&self, query: &str) -> Vec<PackageInfo> {
        if query.trim().is_empty() {
            return Vec::new();
        }
        let installed = by_name(self.list_installed());
        parse_find(&run("snap", &["find", query], process::SHORT).stdout)
            .into_iter()
            .map(|mut pkg| {
                if let Some(i) = installed.get(&pkg.name) {
                    pkg.installed = true;
                    pkg.installed_version = i.installed_version.clone();
                }
                pkg
            })
            .collect()
    }

    fn package_details(&self, names: &[String]) -> Vec<PackageInfo> {
        // One call per name: the single-snap `snap info` format is the stable one.
        names
            .iter()
            .filter_map(|name| {
                let out = run("snap", &["info", name], process::SHORT);
                if !out.success() {
                    return None;
                }
                let (fields, description) = parse_info(&out.stdout);
                let get = |k: &str| fields.get(k).cloned().unwrap_or_default();
                let name = get("name");
                if name.is_empty() {
                    return None;
                }
                let installed_line = get("installed");
                let summary = get("summary");
                let mut repository = get("publisher");
                if repository.is_empty() {
                    repository = get("tracking");
                }
                Some(PackageInfo {
                    name,
                    long_description: if description.is_empty() { summary.clone() } else { description },
                    description: summary,
                    repository,
                    installed: !installed_line.is_empty(),
                    installed_version: installed_line.split_whitespace().next().unwrap_or("").to_owned(),
                    ..Default::default()
                })
            })
            .collect()
    }

    fn install_commands(&self, names: &[String]) -> Vec<Command> {
        vec![Command::privileged(with_names(&["snap", "install"], names))]
    }

    fn remove_commands(&self, names: &[String]) -> Vec<Command> {
        vec![Command::privileged(with_names(&["snap", "remove"], names))]
    }

    fn reinstall_commands(&self, names: &[String]) -> Vec<Command> {
        let mut commands = self.remove_commands(names);
        commands.extend(self.install_commands(names));
        commands
    }

    fn list_updates(&self) -> Vec<PackageInfo> {
        let out = run("snap", &["refresh", "--list"], process::SHORT);
        if out.combined().to_lowercase().contains("up to date") {
            return Vec::new();
        }
        let installed = by_name(self.list_installed());
        data_lines(&out.stdout)
            .map(|l| l.split_whitespace().collect::<Vec<_>>())
            .filter(|f| f.len() >= 2)
            .map(|f| {
                let mut pkg = PackageInfo { name: f[0].to_owned(), available_version: f[1].to_owned(), installed: true, ..Default::default() };
                if let Some(i) = installed.get(&pkg.name) {
                    pkg.installed_version = i.installed_version.clone();
                    pkg.repository = i.repository.clone();
                }
                pkg
            })
            .collect()
    }

    fn upgrade_commands(&self, names: &[String]) -> Vec<Command> {
        vec![Command::privileged(with_names(&["snap", "refresh"], names))]
    }

    fn clean_unused_commands(&self) -> Vec<Command> {
        // The closest thing to left-behind cruft: old revisions kept for rollback.
        data_lines(&run("snap", &["list", "--all"], process::SHORT).stdout)
            .map(|l| l.split_whitespace().collect::<Vec<_>>())
            .filter(|f| f.len() >= 6 && f[f.len() - 1].contains("disabled"))
            .map(|f| Command::privileged(["snap".to_owned(), "remove".into(), f[0].to_owned(), format!("--revision={}", f[2])]))
            .collect()
    }

    fn download_commands(&self, names: &[String], destination: &str, include_dependencies: bool) -> Vec<Command> {
        let mut all = names.to_vec();
        if include_dependencies {
            // A snap's only dependency besides itself is its base snap.
            let mut bases = HashSet::new();
            for name in names {
                let out = run("snap", &["info", name], process::SHORT);
                if let Some(base) = parse_info(&out.stdout).0.get("base").filter(|b| !b.is_empty()) {
                    if bases.insert(base.clone()) && !all.contains(base) {
                        all.push(base.clone());
                    }
                }
            }
        }
        // `snap download` writes to its working directory. Unprivileged.
        let command = Command::new("snap", with_names(&["download"], &all));
        vec![if destination.is_empty() { command } else { command.in_dir(destination) }]
    }

    fn recent_history(&self) -> String {
        let out = run("snap", &["changes"], process::SHORT);
        let mut lines: Vec<&str> = lines(&out.stdout).collect();
        if lines.is_empty() {
            return out.stdout.trim().to_owned();
        }
        let header = lines.remove(0);
        lines.reverse();
        std::iter::once(header).chain(lines).collect::<Vec<_>>().join("\n")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn find() {
        let r = parse_find("Name     Version  Publisher   Notes  Summary\nfirefox  131.0    mozilla**   -      Mozilla Firefox web browser\n");
        assert_eq!(r.len(), 1);
        assert_eq!(r[0].description, "Mozilla Firefox web browser");
    }

    #[test]
    fn info() {
        let (f, d) = parse_info("name:      firefox\nsummary:   Mozilla Firefox\ndescription: |\n  Line one\n  Line two\nbase:      core22\n");
        assert_eq!(f["base"], "core22");
        assert_eq!(d, "Line one\nLine two");
    }
}
