//! dnf5, or dnf (Fedora, RHEL, and derivatives).

use std::collections::{BTreeMap, HashMap, HashSet};
use std::sync::LazyLock;

use regex::Regex;

use super::{by_name, human_size, with_names};
use crate::backend::{Backend, field};
use crate::package::{OperationResult, PackageGroupInfo, PackageInfo, RepositoryAddField, RepositoryInfo, TransactionPreview};
use crate::process::{self, Command, executable_exists, lines, run};

pub struct DnfBackend {
    executable: &'static str,
    /// How stale repo metadata may get before a query re-fetches it.
    metadata_expire_hours: u32,
}

impl Default for DnfBackend {
    fn default() -> Self {
        Self::new()
    }
}

impl DnfBackend {
    pub fn new() -> Self {
        Self {
            executable: if executable_exists("dnf5") { "dnf5" } else { "dnf" },
            metadata_expire_hours: 24,
        }
    }

    pub fn with_metadata_expire_hours(mut self, hours: u32) -> Self {
        self.metadata_expire_hours = hours;
        self
    }

    fn metadata_expire_arg(&self) -> String {
        format!("--setopt=metadata_expire={}h", self.metadata_expire_hours)
    }

    fn dnf(&self, args: &[String]) -> process::Output {
        run(self.executable, args, process::LONG)
    }

    fn privileged(&self, base: &[&str], names: &[String]) -> Command {
        let mut args = vec![self.executable.to_owned()];
        args.extend(with_names(base, names));
        Command::privileged(args)
    }
}

/// "name.arch" → (name, arch).
fn split_arch(name_arch: &str) -> (String, String) {
    match name_arch.rfind('.') {
        Some(dot) if dot > 0 => (name_arch[..dot].to_owned(), name_arch[dot + 1..].to_owned()),
        _ => (name_arch.to_owned(), String::new()),
    }
}

/// dnf5's "Key : value" blocks with continuation lines (`group info`,
/// `environment info`); only the first record, since a group known to two
/// repos is printed once per repo.
pub(crate) fn parse_key_value_block(text: &str) -> BTreeMap<String, Vec<String>> {
    let mut fields: BTreeMap<String, Vec<String>> = BTreeMap::new();
    let mut last_key = String::new();
    let mut saw_content = false;
    for line in text.lines() {
        if line.trim().is_empty() {
            if saw_content {
                break;
            }
            continue;
        }
        let Some((key, value)) = line.split_once(':') else { continue };
        saw_content = true;
        let (key, value) = (key.trim(), value.trim());
        if key.is_empty() {
            if !last_key.is_empty() && !value.is_empty() {
                fields.entry(last_key.clone()).or_default().push(value.to_owned());
            }
            continue;
        }
        last_key = key.to_owned();
        let entry = fields.entry(key.to_owned()).or_default();
        if !value.is_empty() {
            entry.push(value.to_owned());
        }
    }
    fields
}

/// The "ID  Name  Installed" table of `group list` / `environment list`,
/// merged by ID (a group from two repos is listed twice).
pub(crate) fn parse_group_table(text: &str, is_meta: bool) -> Vec<PackageGroupInfo> {
    // \s+, not \s{2,}: a long ID can overflow its column and leave one space.
    static ROW: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^(\S+)\s+(.+?)\s+(yes|no)\s*$").unwrap());
    let mut groups: Vec<PackageGroupInfo> = Vec::new();
    let mut index: HashMap<String, usize> = HashMap::new();
    for m in lines(text).filter_map(|l| ROW.captures(l)) {
        let installed = &m[3] == "yes";
        if let Some(&i) = index.get(&m[1]) {
            groups[i].installed |= installed;
            continue;
        }
        index.insert(m[1].to_owned(), groups.len());
        groups.push(PackageGroupInfo {
            id: m[1].to_owned(),
            name: m[2].trim().to_owned(),
            installed,
            is_meta,
            ..Default::default()
        });
    }
    groups
}

/// `dnf info <names>`: blocks under "Installed packages" / "Available
/// packages" headers; a package can be in both (installed, with a newer
/// version available).
pub(crate) fn parse_package_info(text: &str) -> Vec<PackageInfo> {
    let mut by_name: BTreeMap<String, PackageInfo> = BTreeMap::new();
    let mut available = false;
    let mut fields: HashMap<String, Vec<String>> = HashMap::new();
    let mut last_key = String::new();
    let flush = |fields: &mut HashMap<String, Vec<String>>, available: bool, by_name: &mut BTreeMap<String, PackageInfo>| {
        let first = |fields: &HashMap<String, Vec<String>>, k: &str| fields.get(k).and_then(|v| v.first()).cloned().unwrap_or_default();
        let name = first(fields, "Name");
        if !name.is_empty() {
            let pkg = by_name.entry(name.clone()).or_default();
            pkg.name = name;
            let (version, release) = (first(fields, "Version"), first(fields, "Release"));
            let full = if release.is_empty() { version } else { format!("{version}-{release}") };
            if available {
                pkg.available_version = full;
            } else {
                pkg.installed_version = full;
                pkg.installed = true;
            }
            if pkg.architecture.is_empty() {
                pkg.architecture = first(fields, "Architecture");
            }
            let repo = if fields.contains_key("From repository") {
                first(fields, "From repository")
            } else {
                first(fields, "Repository")
            };
            if !repo.is_empty() {
                pkg.repository = repo;
            }
            if pkg.description.is_empty() {
                pkg.description = first(fields, "Summary");
            }
            if pkg.long_description.is_empty() {
                pkg.long_description = fields.get("Description").map(|v| v.join(" ")).unwrap_or_default();
            }
            if pkg.size.is_empty() {
                pkg.size = first(fields, "Size");
            }
            if pkg.homepage_url.is_empty() {
                pkg.homepage_url = first(fields, "URL");
            }
        }
        fields.clear();
    };
    for line in text.lines() {
        let trimmed = line.trim();
        if trimmed.eq_ignore_ascii_case("Installed packages") || trimmed.eq_ignore_ascii_case("Available packages") {
            flush(&mut fields, available, &mut by_name);
            available = trimmed.eq_ignore_ascii_case("Available packages");
            last_key.clear();
            continue;
        }
        if trimmed.is_empty() {
            flush(&mut fields, available, &mut by_name);
            last_key.clear();
            continue;
        }
        let Some((key, value)) = line.split_once(':') else { continue };
        let (key, value) = (key.trim(), value.trim());
        if key.is_empty() {
            if !last_key.is_empty() && !value.is_empty() {
                fields.entry(last_key.clone()).or_default().push(value.to_owned());
            }
            continue;
        }
        last_key = key.to_owned();
        if !value.is_empty() {
            fields.entry(key.to_owned()).or_default().push(value.to_owned());
        }
    }
    flush(&mut fields, available, &mut by_name);
    by_name.into_values().collect()
}

/// `dnf --assumeno` output without the repository-loading noise.
fn clean_plan(raw: &str) -> String {
    raw.lines()
        .filter(|l| {
            let t = l.trim();
            !t.is_empty() && t != "Updating and loading repositories:" && t != "Repositories loaded." && t != "Operation aborted by the user."
        })
        .collect::<Vec<_>>()
        .join("\n")
        .trim()
        .to_owned()
}

/// `dnf list --upgrades`: "name.arch version repo".
pub(crate) fn parse_upgrades(text: &str) -> Vec<PackageInfo> {
    lines(text)
        .filter_map(|l| {
            let f: Vec<&str> = l.split_whitespace().collect();
            // Versions start with a digit; anything else is a header.
            if f.len() < 3 || !f[1].starts_with(|c: char| c.is_ascii_digit()) {
                return None;
            }
            let (name, architecture) = split_arch(f[0]);
            Some(PackageInfo {
                name,
                architecture,
                available_version: f[1].to_owned(),
                repository: f[2].to_owned(),
                installed: true,
                ..Default::default()
            })
        })
        .collect()
}

impl Backend for DnfBackend {
    fn key(&self) -> &'static str {
        "dnf"
    }

    fn name(&self) -> &'static str {
        "DNF (Fedora / RHEL)"
    }

    fn is_available(&self) -> bool {
        executable_exists("dnf5") || executable_exists("dnf")
    }

    fn list_installed(&self) -> Vec<PackageInfo> {
        // rpm reads the local database directly; much faster than dnf.
        let out = run(
            "rpm",
            &["-qa", "--queryformat", "%{NAME}\t%{VERSION}-%{RELEASE}\t%{ARCH}\t%{SIZE}\t%{URL}\t%{SUMMARY}\n"],
            process::LONG,
        );
        let mut packages: Vec<PackageInfo> = lines(&out.stdout)
            .map(|l| l.split('\t').collect::<Vec<_>>())
            .filter(|f| f.len() >= 6)
            .map(|f| PackageInfo {
                name: f[0].to_owned(),
                installed_version: f[1].to_owned(),
                architecture: f[2].to_owned(),
                size: f[3].parse::<u64>().ok().filter(|s| *s > 0).map(human_size).unwrap_or_default(),
                homepage_url: if f[4] == "(none)" { String::new() } else { f[4].to_owned() },
                description: f[5].to_owned(),
                repository: "installed".into(),
                installed: true,
                ..Default::default()
            })
            .collect();
        packages.sort_by(|a, b| a.name.cmp(&b.name));
        packages
    }

    fn search(&self, query: &str) -> Vec<PackageInfo> {
        let out = self.dnf(&["search".into(), query.into()]);
        let installed = by_name(self.list_installed());
        lines(&out.stdout)
            .filter_map(|l| l.split_once('\t')) // skips "Matched fields:" headers
            .map(|(name_arch, summary)| {
                let (name, architecture) = split_arch(name_arch.trim());
                let mut pkg = PackageInfo {
                    name,
                    architecture,
                    description: summary.trim().to_owned(),
                    repository: "available".into(),
                    ..Default::default()
                };
                if let Some(i) = installed.get(&pkg.name) {
                    pkg.installed = true;
                    pkg.installed_version = i.installed_version.clone();
                    pkg.repository = "installed".into();
                    pkg.size = i.size.clone();
                    pkg.homepage_url = i.homepage_url.clone();
                }
                pkg
            })
            .collect()
    }

    fn dependency_query(&self, capability: &str, find_requires: bool) -> Vec<PackageInfo> {
        if capability.trim().is_empty() {
            return Vec::new();
        }
        let args: Vec<String> = [
            "repoquery",
            if find_requires { "--whatrequires" } else { "--whatprovides" },
            capability,
            "--queryformat",
            "%{name}\t%{arch}\t%{reponame}\t%{summary}\n",
        ]
        .iter()
        .map(|s| (*s).to_owned())
        .collect();
        let out = self.dnf(&args);
        let installed = by_name(self.list_installed());
        let mut seen = HashSet::new();
        lines(&out.stdout)
            .map(|l| l.split('\t').collect::<Vec<_>>())
            .filter(|f| f.len() >= 4 && seen.insert(f[0].to_owned()))
            .map(|f| {
                let mut pkg = PackageInfo {
                    name: f[0].to_owned(),
                    architecture: f[1].to_owned(),
                    repository: f[2].to_owned(),
                    description: f[3].to_owned(),
                    ..Default::default()
                };
                if let Some(i) = installed.get(&pkg.name) {
                    pkg.installed = true;
                    pkg.installed_version = i.installed_version.clone();
                    pkg.size = i.size.clone();
                    pkg.homepage_url = i.homepage_url.clone();
                    pkg.repository = "installed".into();
                }
                pkg
            })
            .collect()
    }

    fn package_details(&self, names: &[String]) -> Vec<PackageInfo> {
        if names.is_empty() {
            return Vec::new();
        }
        parse_package_info(&self.dnf(&with_names(&["info"], names)).stdout)
    }

    fn list_groups(&self) -> Vec<PackageGroupInfo> {
        let mut groups = parse_group_table(&self.dnf(&["environment".into(), "list".into()]).stdout, true);
        groups.extend(parse_group_table(&self.dnf(&["group".into(), "list".into()]).stdout, false));
        groups
    }

    fn group_details(&self, id: &str, is_meta: bool) -> PackageGroupInfo {
        let kind = if is_meta { "environment" } else { "group" };
        let fields = parse_key_value_block(&self.dnf(&[kind.into(), "info".into(), id.into()]).stdout);
        let first = |k: &str| fields.get(k).and_then(|v| v.first()).cloned();
        let mut group = PackageGroupInfo {
            id: id.to_owned(),
            name: first("Name").unwrap_or_else(|| id.to_owned()),
            is_meta,
            ..Default::default()
        };
        group.description = fields.get("Description").map(|v| v.join(" ")).unwrap_or_default();
        group.installed = first("Installed").is_some_and(|v| v.eq_ignore_ascii_case("yes") || v.eq_ignore_ascii_case("true"));
        for (key, values) in &fields {
            let key = key.to_lowercase();
            if is_meta && key.contains("groups") {
                group.sub_groups.extend(values.iter().cloned());
            } else if !is_meta && key.contains("packages") {
                group.packages.extend(values.iter().cloned());
            }
        }
        group
    }

    fn install_commands(&self, names: &[String]) -> Vec<Command> {
        vec![self.privileged(&["install", "-y"], names)]
    }

    fn remove_commands(&self, names: &[String]) -> Vec<Command> {
        // Explicitly, whatever dnf.conf says: also remove now-unused dependencies.
        vec![self.privileged(&["remove", "-y", "--setopt=clean_requirements_on_remove=True"], names)]
    }

    fn reinstall_commands(&self, names: &[String]) -> Vec<Command> {
        vec![self.privileged(&["reinstall", "-y"], names)]
    }

    fn list_updates(&self) -> Vec<PackageInfo> {
        let out = self.dnf(&["list".into(), "--upgrades".into(), self.metadata_expire_arg()]);
        let installed = by_name(self.list_installed());
        parse_upgrades(&out.stdout)
            .into_iter()
            .map(|mut pkg| {
                if let Some(i) = installed.get(&pkg.name) {
                    pkg.installed_version = i.installed_version.clone();
                    pkg.description = i.description.clone();
                    // Not the size: that describes the old version.
                    pkg.homepage_url = i.homepage_url.clone();
                }
                pkg
            })
            .collect()
    }

    fn upgrade_commands(&self, names: &[String]) -> Vec<Command> {
        vec![self.privileged(&["upgrade", "-y"], names)]
    }

    fn clean_unused_commands(&self) -> Vec<Command> {
        vec![self.privileged(&["autoremove", "-y"], &[])]
    }

    fn download_commands(&self, names: &[String], destination: &str, include_dependencies: bool) -> Vec<Command> {
        // Unprivileged: reads cached metadata, writes plain files.
        let mut args = vec!["download".to_owned()];
        if !destination.is_empty() {
            args.push(format!("--destdir={destination}"));
        }
        if include_dependencies {
            args.push("--resolve".into());
        }
        args.extend(names.iter().cloned());
        vec![Command::new(self.executable, args)]
    }

    fn preview_install(&self, names: &[String]) -> TransactionPreview {
        let out = self.dnf(&with_names(&["--assumeno", "install"], names));
        TransactionPreview {
            available: out.started,
            plan_text: clean_plan(&out.stdout),
        }
    }

    fn preview_remove(&self, names: &[String]) -> TransactionPreview {
        let out = self.dnf(&with_names(&["--assumeno", "remove", "--setopt=clean_requirements_on_remove=True"], names));
        TransactionPreview {
            available: out.started,
            plan_text: clean_plan(&out.stdout),
        }
    }

    fn refresh_metadata_commands(&self) -> Vec<Command> {
        vec![Command::privileged([
            self.executable.to_owned(),
            "makecache".into(),
            self.metadata_expire_arg(),
        ])]
    }

    fn recent_history(&self) -> String {
        self.dnf(&["history".into(), "list".into()]).stdout.trim().to_owned()
    }

    fn list_repositories(&self) -> Vec<RepositoryInfo> {
        static ROW: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^(\S+)\s+(.+?)\s+(enabled|disabled)\s*$").unwrap());
        lines(&self.dnf(&["repo".into(), "list".into(), "--all".into()]).stdout)
            .filter_map(|l| ROW.captures(l))
            .map(|m| RepositoryInfo {
                id: m[1].to_owned(),
                name: m[2].trim().to_owned(),
                url: String::new(),
                enabled: &m[3] == "enabled",
            })
            .collect()
    }

    fn set_repository_enabled(&self, id: &str, enabled: bool) -> OperationResult {
        let out = run(
            "pkexec",
            &[self.executable, "config-manager", if enabled { "enable" } else { "disable" }, id],
            process::LONG,
        );
        OperationResult {
            success: out.success(),
            output: out.combined(),
        }
    }

    fn repository_add_fields(&self) -> Vec<RepositoryAddField> {
        // Two ways in one form: a COPR, or an ID and base URL.
        vec![
            RepositoryAddField {
                key: "copr",
                label: "COPR repository (owner/project)",
                placeholder: "owner/project",
                required: false,
            },
            RepositoryAddField {
                key: "id",
                label: "Repository ID",
                placeholder: "myrepo",
                required: false,
            },
            RepositoryAddField {
                key: "baseurl",
                label: "Base URL",
                placeholder: "https://example.com/repo/",
                required: false,
            },
            RepositoryAddField {
                key: "name",
                label: "Display name (optional)",
                placeholder: "My Repo",
                required: false,
            },
        ]
    }

    fn add_repository(&self, values: &[(String, String)]) -> OperationResult {
        static COPR: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^[\w.-]+/[\w.-]+$").unwrap());
        let (copr, id, base, name) = (field(values, "copr"), field(values, "id"), field(values, "baseurl"), field(values, "name"));
        let out = if !copr.is_empty() {
            if !COPR.is_match(copr) {
                return OperationResult::failed("COPR repository must be in \"owner/project\" form (e.g. user/myrepo).");
            }
            run("pkexec", &[self.executable, "copr", "enable", "-y", copr], process::LONG)
        } else if id.is_empty() || base.is_empty() {
            return OperationResult::failed("Provide either a COPR repository (owner/project) or a Repository ID and Base URL.");
        } else {
            let mut args = vec![
                self.executable.to_owned(),
                "config-manager".into(),
                "addrepo".into(),
                format!("--id={id}"),
                format!("--set=baseurl={base}"),
            ];
            if !name.is_empty() {
                args.push(format!("--set=name={name}"));
            }
            run("pkexec", &args, process::LONG)
        };
        OperationResult {
            success: out.success(),
            output: out.combined(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn groups_merge_by_id() {
        let text = "ID                   Name                 Installed\nkde-desktop          KDE                  no\nkde-desktop          KDE                  yes\ncopr:very-long-id Copr thing no\n";
        let g = parse_group_table(text, false);
        assert_eq!(g.len(), 2);
        assert!(g[0].installed);
        assert_eq!(g[1].name, "Copr thing");
    }

    #[test]
    fn info_both_sections() {
        let text = "Installed packages\nName            : firefox\nVersion         : 130.0\nRelease         : 1.fc44\nSummary         : Mozilla Firefox\nDescription     : A browser\n                : that is fast\n\nAvailable packages\nName            : firefox\nVersion         : 131.0\nRelease         : 1.fc44\nRepository      : updates\n";
        let p = parse_package_info(text);
        assert_eq!(p.len(), 1);
        assert_eq!(p[0].installed_version, "130.0-1.fc44");
        assert_eq!(p[0].available_version, "131.0-1.fc44");
        assert_eq!(p[0].long_description, "A browser that is fast");
    }

    #[test]
    fn upgrades_skip_headers() {
        let u = parse_upgrades("Available upgrades\nfirefox.x86_64 131.0-1.fc44 updates\n");
        assert_eq!(u.len(), 1);
        assert_eq!((u[0].name.as_str(), u[0].architecture.as_str()), ("firefox", "x86_64"));
    }

    #[test]
    fn key_value_first_record_only() {
        let f = parse_key_value_block("Id   : kde\nName : KDE\nMandatory packages : plasma\n                   : dolphin\n\nId   : kde\n");
        assert_eq!(f["Mandatory packages"], ["plasma", "dolphin"]);
    }
}
