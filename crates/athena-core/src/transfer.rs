//! Versioned package manifests preserve source, architecture and Flatpak scope.
use crate::{
    Backend,
    process::{self, run},
};
use serde::{Deserialize, Serialize};
use std::{
    collections::{BTreeSet, HashSet},
    sync::Arc,
};
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct Package {
    pub source: String,
    pub name: String,
    #[serde(default)]
    pub repository: String,
    #[serde(default)]
    pub reference: String,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Manifest {
    pub version: u32,
    pub packages: Vec<Package>,
}
fn query(program: &str, args: &[&str]) -> Result<String, String> {
    let out = run(program, args, process::LONG);
    if out.success() {
        Ok(out.stdout)
    } else {
        Err(format!("{program}: {}", out.combined()))
    }
}
fn names(text: &str) -> BTreeSet<String> {
    text.lines().map(str::trim).filter(|s| !s.is_empty()).map(str::to_owned).collect()
}
impl Manifest {
    pub fn collect(backends: &[Arc<dyn Backend>]) -> Result<Self, String> {
        let mut packages = Vec::new();

        for backend in backends {
            let key = backend.key();
            if key == "aur" {
                continue;
            }
            match key {
                "flatpak" => {
                    for line in query("flatpak", &["list", "--app", "--columns=ref,origin,installation"])?.lines() {
                        let fields: Vec<_> = line.split('\t').collect();
                        if fields.len() < 3 {
                            return Err("Flatpak did not report installation scope".into());
                        }
                        let reference = if fields[0].starts_with("app/") {
                            fields[0].to_owned()
                        } else {
                            format!("app/{}", fields[0])
                        };
                        let name = reference.split('/').nth(1).unwrap_or_default().to_owned();
                        packages.push(Package {
                            source: key.into(),
                            name,
                            repository: format!(
                                "{}:{}",
                                if ["user", "system"].contains(&fields[2]) {
                                    fields[2].to_owned()
                                } else {
                                    format!("installation={}", fields[2])
                                },
                                fields[1]
                            ),
                            reference,
                        });
                    }
                }
                "pacman" => {
                    let foreign = names(&run("pacman", &["-Qqm"], process::SHORT).stdout);
                    for name in names(&query("pacman", &["-Qqe"])?) {
                        packages.push(Package {
                            source: if foreign.contains(&name) { "aur" } else { key }.into(),
                            name,
                            repository: String::new(),
                            reference: String::new(),
                        });
                    }
                }
                "apt" | "dnf" => {
                    let explicit = if key == "apt" {
                        names(&query("apt-mark", &["showmanual"])?)
                    } else {
                        names(&query(crate::cleanup::dnf_program(), &["repoquery", "--userinstalled", "--qf", "%{name}"])?)
                    };
                    for p in backend
                        .list_installed()
                        .into_iter()
                        .filter(|p| explicit.contains(&p.name) || explicit.contains(&format!("{}:{}", p.name, p.architecture)))
                    {
                        let name = if key == "apt" && !p.architecture.is_empty() {
                            format!("{}:{}", p.name, p.architecture)
                        } else if key == "dnf" && !p.architecture.is_empty() {
                            format!("{}.{}", p.name, p.architecture)
                        } else {
                            p.name
                        };
                        packages.push(Package {
                            source: key.into(),
                            name,
                            repository: String::new(),
                            reference: String::new(),
                        });
                    }
                }
                "snap" => {
                    // Snap's API distinguishes application snaps from automatically
                    // installed base/kernel/gadget/snapd support snaps.
                    let text = query("snap", &["debug", "api", "/v2/snaps"])?;
                    let value: serde_json::Value = serde_json::from_str(&text).map_err(|e| e.to_string())?;
                    let apps = value
                        .get("result")
                        .and_then(|v| v.as_array())
                        .ok_or("Snap did not return an installed app list")?;
                    for app in apps.iter().filter(|p| p["type"].as_str() == Some("app")) {
                        let name = app["name"].as_str().ok_or("Snap app has no name")?.to_owned();
                        packages.push(Package {
                            source: key.into(),
                            name,
                            repository: String::new(),
                            reference: String::new(),
                        });
                    }
                }
                _ => return Err(format!("Unsupported source: {key}")),
            }
        }
        let manifest = Self { version: 1, packages };
        manifest.validate()?;
        Ok(manifest)
    }
    pub fn parse(text: &str) -> Result<Self, String> {
        if text.len() > 16 * 1024 * 1024 {
            return Err("Package list is too large".into());
        }
        let value: Self = serde_json::from_str(text).map_err(|e| e.to_string())?;
        value.validate()?;
        Ok(value)
    }
    pub fn json(&self) -> Result<String, String> {
        self.validate()?;
        serde_json::to_string_pretty(self).map_err(|e| e.to_string())
    }
    fn validate(&self) -> Result<(), String> {
        if self.version != 1 || self.packages.len() > 100_000 {
            return Err("Unsupported or oversized package list".into());
        }
        for p in &self.packages {
            if !["pacman", "apt", "dnf", "flatpak", "snap", "aur"].contains(&p.source.as_str())
                || p.name.is_empty()
                || p.name.starts_with('-')
                || p.name.len() > 512
                || !p.name.chars().all(|c| c.is_ascii_alphanumeric() || "@._+:-".contains(c))
            {
                return Err(format!("Invalid source or package name: {}", p.name));
            }
            if p.source == "flatpak" {
                let Some((_, remote)) = p.repository.split_once(':') else {
                    return Err("Flatpak scope is required".into());
                };
                let fields: Vec<_> = p.reference.split('/').collect();
                if remote.is_empty()
                    || remote.starts_with('-')
                    || !remote.bytes().all(|b| b.is_ascii_alphanumeric() || b"._-".contains(&b))
                    || fields.len() != 4
                    || fields[0] != "app"
                    || fields[1] != p.name
                    || fields[2..]
                        .iter()
                        .any(|f| f.is_empty() || !f.bytes().all(|b| b.is_ascii_alphanumeric() || b"._-".contains(&b)))
                    || crate::source_tools::flatpak_commands("install", &p.name, &p.repository, &p.reference).is_none()
                {
                    return Err("Invalid Flatpak source/ref".into());
                }
            }
        }
        Ok(())
    }
    pub fn missing(&self, backends: &[Arc<dyn Backend>]) -> Vec<(Package, Option<usize>)> {
        let installed: Vec<HashSet<String>> = backends
            .iter()
            .map(|b| {
                b.list_installed()
                    .into_iter()
                    .flat_map(|p| {
                        [
                            p.name.clone(),
                            format!("{}:{}", p.name, p.architecture),
                            format!("{}.{}", p.name, p.architecture),
                        ]
                    })
                    .collect()
            })
            .collect();
        self.packages
            .iter()
            .filter_map(|p| {
                let source = backends.iter().position(|b| b.key() == p.source);
                let present = if p.source == "flatpak" {
                    let scope = p.repository.split(':').next().unwrap_or("system");
                    let arg = if ["system", "user"].contains(&scope) {
                        format!("--{scope}")
                    } else {
                        format!("--installation={}", scope.trim_start_matches("installation="))
                    };
                    source.is_some() && run("flatpak", &["info", &arg, "--", &p.reference], process::SHORT).success()
                } else {
                    source.is_some_and(|s| installed[s].contains(&p.name))
                        || p.source == "aur" && backends.iter().enumerate().any(|(s, b)| b.key() == "pacman" && installed[s].contains(&p.name))
                };
                (!present).then(|| (p.clone(), source))
            })
            .collect()
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rejects_unknown_versions_sources_and_options() {
        for text in [
            r#"{"version":2,"packages":[]}"#,
            r#"{"version":1,"packages":[{"source":"shell","name":"foo"}]}"#,
            r#"{"version":1,"packages":[{"source":"apt","name":"--allow-unauthenticated"}]}"#,
        ] {
            assert!(Manifest::parse(text).is_err());
        }
    }
    #[test]
    fn scoped_flatpak_roundtrip() {
        let m = Manifest {
            version: 1,
            packages: vec![Package {
                source: "flatpak".into(),
                name: "org.example.App".into(),
                repository: "user:flathub".into(),
                reference: "app/org.example.App/x86_64/stable".into(),
            }],
        };
        assert_eq!(Manifest::parse(&m.json().unwrap()).unwrap().packages, m.packages);
    }
}

#[cfg(test)]
mod untrusted_manifests {
    use super::*;
    #[test]
    fn refuses_remote_urls_and_mismatched_flatpak_ids() {
        for (repository, reference) in [
            ("user:https://untrusted.invalid/repo", "app/org.example.App/x86_64/stable"),
            ("system:flathub", "app/org.other.App/x86_64/stable"),
            ("user:flathub", "/tmp/example.flatpakref"),
            ("unknown:flathub", "app/org.example.App/x86_64/stable"),
        ] {
            let m = Manifest {
                version: 1,
                packages: vec![Package {
                    source: "flatpak".into(),
                    name: "org.example.App".into(),
                    repository: repository.into(),
                    reference: reference.into(),
                }],
            };
            assert!(m.json().is_err());
        }
    }
}
