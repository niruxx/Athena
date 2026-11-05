//! AppStream XML and DEP-11 catalogs from the native system and Flatpak.
use crate::{PackageInfo, media::AppMetadata};
use serde::Deserialize;
use std::{
    collections::{HashMap, HashSet},
    fs,
    io::Read,
    path::{Path, PathBuf},
    sync::{Arc, LazyLock, RwLock},
};

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Offer {
    pub backend: String,
    pub package: String,
    pub repository: String,
    pub scope: String,
    pub architecture: String,
    pub reference: String,
}
#[derive(Debug, Clone, Default)]
pub struct App {
    pub id: String,
    pub name: String,
    pub summary: String,
    pub description: String,
    pub homepage: String,
    pub icon: String,
    pub categories: Vec<String>,
    pub screenshots: Vec<String>,
    pub aliases: Vec<String>,
    pub offers: Vec<Offer>,
}
#[derive(Debug, Default)]
pub struct Catalog {
    pub apps: Vec<App>,
    pub warnings: Vec<String>,
    lookup: HashMap<String, usize>,
}
#[derive(Debug, Clone, Default)]
pub struct Input {
    pub path: PathBuf,
    pub backend: String,
    pub repository: String,
    pub scope: String,
    pub architecture: String,
    pub icon_root: PathBuf,
}

static CURRENT: LazyLock<RwLock<Arc<Catalog>>> = LazyLock::new(|| RwLock::new(Arc::default()));
pub fn current() -> Arc<Catalog> {
    CURRENT.read().unwrap().clone()
}
pub fn reload(native: &str) -> Arc<Catalog> {
    let catalog = Arc::new(load(native));
    *CURRENT.write().unwrap() = catalog.clone();
    catalog
}

fn identity(id: &str) -> String {
    id.trim_end_matches(".desktop").to_lowercase()
}
impl Catalog {
    pub fn find(&self, package: &str) -> Option<&App> {
        self.lookup.get(&identity(package)).and_then(|i| self.apps.get(*i))
    }
    pub fn metadata(&self, package: &str) -> Option<AppMetadata> {
        self.find(package).map(|app| AppMetadata {
            id: app.id.clone(),
            packages: app.offers.iter().map(|o| o.package.clone()).collect(),
            screenshots: app.screenshots.clone(),
            description: app.description.clone(),
            homepage: app.homepage.clone(),
        })
    }
    pub fn from_apps(mut apps: Vec<App>) -> Self {
        let native_names: HashMap<String, String> = apps
            .iter()
            .flat_map(|app| {
                app.offers
                    .iter()
                    .filter(|o| o.backend != "flatpak")
                    .map(|o| (identity(&o.package), app.name.to_lowercase()))
            })
            .collect();
        let mut short_ids = HashMap::<String, HashSet<String>>::new();
        for app in &apps {
            if app.offers.iter().any(|o| o.backend == "flatpak") {
                short_ids
                    .entry(identity(&app.id).rsplit('.').next().unwrap_or_default().to_owned())
                    .or_default()
                    .insert(identity(&app.id));
            }
        }
        for app in &mut apps {
            app.aliases.push(identity(&app.id));
            for offer in &app.offers {
                app.aliases.push(identity(&offer.package));
            }
            if let Some(short) = identity(&app.id).rsplit('.').next()
                && short_ids.get(short).is_none_or(|ids| ids.len() == 1) && native_names.get(short).is_some_and(|name| name == &app.name.to_lowercase()) {
                    app.aliases.push(short.into());
                }
            app.aliases = app.aliases.iter().map(|a| identity(a)).filter(|a| !a.is_empty()).collect();
            app.aliases.sort();
            app.aliases.dedup();
        }
        let mut parents: Vec<_> = (0..apps.len()).collect();
        let mut aliases = HashMap::new();
        fn root(parents: &[usize], mut index: usize) -> usize {
            while parents[index] != index {
                index = parents[index];
            }
            index
        }
        for (index, app) in apps.iter().enumerate() {
            for alias in &app.aliases {
                if let Some(previous) = aliases.insert(alias.clone(), index) {
                    let a = root(&parents, index);
                    let b = root(&parents, previous);
                    parents[a] = b;
                }
            }
        }
        let mut groups = std::collections::BTreeMap::<usize, App>::new();
        for (index, app) in apps.into_iter().enumerate() {
            let target = groups.entry(root(&parents, index)).or_default();
            if target.id.is_empty() {
                *target = app;
                continue;
            }
            target.aliases.extend(app.aliases);
            target.categories.extend(app.categories);
            target.categories.sort();
            target.categories.dedup();
            for offer in app.offers {
                if !target.offers.contains(&offer) {
                    target.offers.push(offer);
                }
            }
            if target.icon.is_empty() {
                target.icon = app.icon;
            }
            if target.summary.is_empty() {
                target.summary = app.summary;
            }
            if target.description.is_empty() {
                target.description = app.description;
            }
            if target.homepage.is_empty() {
                target.homepage = app.homepage;
            }
            for screenshot in app.screenshots {
                if !target.screenshots.contains(&screenshot) {
                    target.screenshots.push(screenshot);
                }
            }
        }
        let mut result = Self {
            apps: groups.into_values().collect(),
            ..Default::default()
        };
        for (index, app) in result.apps.iter().enumerate() {
            for alias in &app.aliases {
                result.lookup.insert(alias.clone(), index);
            }
        }
        let aliases: Vec<_> = result
            .lookup
            .iter()
            .filter(|(key, _)| !key.contains('.'))
            .map(|(key, index)| (format!("{key}-bin"), *index))
            .collect();
        for (alias, index) in aliases {
            result.lookup.entry(alias).or_insert(index);
        }
        result
    }
}

fn unlocalized(node: roxmltree::Node<'_, '_>, tag: &str) -> String {
    node.children()
        .find(|n| n.has_tag_name(tag) && n.attribute(("http://www.w3.org/XML/1998/namespace", "lang")).is_none())
        .and_then(|n| n.text())
        .unwrap_or_default()
        .trim()
        .to_owned()
}
fn absolute_media(base: &str, url: &str) -> String {
    if url.starts_with("https://") {
        url.into()
    } else if base.starts_with("https://") && !url.contains("://") {
        format!("{}/{}", base.trim_end_matches('/'), url.trim_start_matches('/'))
    } else {
        String::new()
    }
}
fn icon_path(input: &Input, origin: &str, name: &str) -> String {
    if name.is_empty() || Path::new(name).is_absolute() || Path::new(name).components().any(|c| !matches!(c, std::path::Component::Normal(_))) {
        return String::new();
    }
    for base in [
        input.icon_root.clone(),
        input
            .icon_root
            .join(if Path::new(origin).components().all(|c| matches!(c, std::path::Component::Normal(_))) {
                origin
            } else {
                ""
            }),
    ] {
        for size in ["128x128", "64x64", "128x128@2", "64x64@2"] {
            let path = base.join(size).join(name);
            if path.is_file() {
                return path.to_string_lossy().into_owned();
            }
        }
    }
    String::new()
}

pub fn parse_xml(xml: &str, input: &Input) -> Result<Vec<App>, String> {
    let doc = roxmltree::Document::parse(xml).map_err(|e| e.to_string())?;
    let root = doc.root_element();
    let origin = root.attribute("origin").unwrap_or(&input.repository);
    let media = root.attribute("media_baseurl").unwrap_or_default();
    let mut apps = Vec::new();
    for node in doc
        .descendants()
        .filter(|n| n.has_tag_name("component") && n.attribute("type") == Some("desktop-application"))
    {
        let id = unlocalized(node, "id");
        if id.is_empty() {
            continue;
        }
        let mut app = App {
            id: id.clone(),
            name: unlocalized(node, "name"),
            summary: unlocalized(node, "summary"),
            ..Default::default()
        };
        for field in node.children() {
            if field.has_tag_name("description") && field.attribute(("http://www.w3.org/XML/1998/namespace", "lang")).is_none() {
                app.description = field
                    .descendants()
                    .filter(|n| n.has_tag_name("p") || n.has_tag_name("li"))
                    .map(|n| n.descendants().filter(|n| n.is_text()).filter_map(|n| n.text()).collect::<String>())
                    .collect::<Vec<_>>()
                    .join("\n\n");
            }
            if field.has_tag_name("url") && field.attribute("type") == Some("homepage") {
                app.homepage = field.text().unwrap_or_default().into();
            }
            if field.has_tag_name("categories") {
                app.categories = field.children().filter_map(|n| n.text()).map(str::to_owned).collect();
            }
            if field.has_tag_name("launchable")
                && let Some(id) = field.text() {
                    app.aliases.push(identity(id));
                }
            if field.has_tag_name("provides") {
                app.aliases
                    .extend(field.children().filter(|n| n.has_tag_name("id")).filter_map(|n| n.text()).map(identity));
            }
            if field.has_tag_name("icon") {
                let name = field.text().unwrap_or_default();
                let icon = match field.attribute("type") {
                    Some("cached") => icon_path(input, origin, name),
                    Some("stock") => name.to_owned(),
                    Some("remote") => absolute_media(media, name),
                    _ => String::new(),
                };
                if app.icon.is_empty() || Path::new(&icon).is_absolute() {
                    app.icon = icon;
                }
            }
            if field.has_tag_name("pkgname") && input.backend != "flatpak"
                && let Some(package) = field.text() {
                    app.offers.push(Offer {
                        backend: input.backend.clone(),
                        package: package.into(),
                        repository: origin.into(),
                        architecture: input.architecture.clone(),
                        ..Default::default()
                    });
                }
            if field.has_tag_name("bundle") && field.attribute("type") == Some("flatpak") && input.backend == "flatpak" {
                let parts = field.text().unwrap_or_default().split('/').collect::<Vec<_>>();
                if parts.len() >= 3 && parts[0] == "app" {
                    app.offers.push(Offer {
                        backend: "flatpak".into(),
                        package: parts[1].into(),
                        repository: input.repository.clone(),
                        scope: input.scope.clone(),
                        architecture: parts[2].into(),
                        reference: field.text().unwrap_or_default().into(),
                    });
                }
            }
        }
        app.screenshots = node
            .descendants()
            .filter(|n| n.has_tag_name("image") && n.attribute("type") != Some("thumbnail"))
            .filter_map(|n| n.text())
            .map(|s| absolute_media(media, s))
            .filter(|s| !s.is_empty())
            .take(5)
            .collect();
        if input.backend == "flatpak" && app.offers.is_empty() {
            app.offers.push(Offer {
                backend: "flatpak".into(),
                package: id.trim_end_matches(".desktop").into(),
                repository: input.repository.clone(),
                scope: input.scope.clone(),
                architecture: input.architecture.clone(),
                reference: String::new(),
            });
        }
        if app.name.is_empty() {
            app.name = id;
        }
        if !app.offers.is_empty() {
            apps.push(app);
        }
    }
    Ok(apps)
}

fn plain_markup(value: &str) -> String {
    let xml = format!("<description>{value}</description>");
    roxmltree::Document::parse(&xml)
        .map(|document| {
            let blocks = document
                .descendants()
                .filter(|n| n.has_tag_name("p") || n.has_tag_name("li"))
                .map(|n| n.descendants().filter(|n| n.is_text()).filter_map(|n| n.text()).collect::<String>())
                .collect::<Vec<_>>();
            if blocks.is_empty() {
                document.descendants().filter(|n| n.is_text()).filter_map(|n| n.text()).collect()
            } else {
                blocks.join("\n\n")
            }
        })
        .unwrap_or_else(|_| regex::Regex::new("<[^>]*>").unwrap().replace_all(value, "").into_owned())
}

fn localized(value: &serde_yaml_ng::Value) -> String {
    value.as_str().or_else(|| value.get("C").and_then(|v| v.as_str())).unwrap_or_default().into()
}
pub fn parse_yaml(text: &str, input: &Input) -> Result<Vec<App>, String> {
    let mut apps = Vec::new();
    let mut origin = input.repository.clone();
    let mut media = String::new();
    for document in serde_yaml_ng::Deserializer::from_str(text) {
        let value = serde_yaml_ng::Value::deserialize(document).map_err(|e| e.to_string())?;
        if value.get("File").is_some() {
            origin = localized(&value["Origin"]);
            media = localized(&value["MediaBaseUrl"]);
            continue;
        }
        if value["Type"].as_str() != Some("desktop-application") {
            continue;
        }
        let id = localized(&value["ID"]);
        let package = localized(&value["Package"]);
        if id.is_empty() || package.is_empty() {
            continue;
        }
        let mut app = App {
            id: id.clone(),
            name: localized(&value["Name"]),
            summary: localized(&value["Summary"]),
            description: plain_markup(&localized(&value["Description"])),
            homepage: localized(&value["Url"]["homepage"]),
            ..Default::default()
        };
        if let Some(categories) = value["Categories"].as_sequence() {
            app.categories = categories.iter().filter_map(|v| v.as_str().map(str::to_owned)).collect();
        }
        let icons = &value["Icon"];
        app.icon = localized(&icons["stock"]);
        if let Some(cached) = icons["cached"].as_str() {
            let path = icon_path(input, &origin, cached);
            if !path.is_empty() {
                app.icon = path;
            }
        }
        if let Some(cached) = icons["cached"].as_sequence() {
            for icon in cached {
                let path = icon_path(input, &origin, &localized(&icon["name"]));
                if !path.is_empty() {
                    app.icon = path;
                    break;
                }
            }
        }
        if app.icon.is_empty()
            && let Some(remote) = icons["remote"].as_sequence().and_then(|s| s.first()) {
                app.icon = absolute_media(&media, &localized(&remote["url"]));
            }
        if let Some(screenshots) = value["Screenshots"].as_sequence() {
            app.screenshots = screenshots
                .iter()
                .filter_map(|v| v.get("source-image"))
                .map(|v| absolute_media(&media, &localized(&v["url"])))
                .filter(|v| !v.is_empty())
                .take(5)
                .collect();
        }
        if let Some(launch) = value["Launchable"]["desktop-id"].as_sequence() {
            app.aliases.extend(launch.iter().filter_map(|v| v.as_str()).map(identity));
        }
        app.offers.push(Offer {
            backend: input.backend.clone(),
            package,
            repository: origin.clone(),
            architecture: input.architecture.clone(),
            ..Default::default()
        });
        if app.name.is_empty() {
            app.name = id;
        }
        apps.push(app);
    }
    Ok(apps)
}

fn native_files(root: &Path, backend: &str, files: &mut Vec<Input>) {
    for entry in fs::read_dir(root).into_iter().flatten().flatten() {
        let path = entry.path();
        let name = entry.file_name().to_string_lossy().into_owned();
        if [".xml", ".xml.gz", ".yml", ".yml.gz", ".yaml", ".yaml.gz"]
            .iter()
            .any(|suffix| name.ends_with(suffix))
        {
            files.push(Input {
                icon_root: root.parent().unwrap_or(root).join("icons"),
                path,
                backend: backend.into(),
                repository: name.split('_').next().unwrap_or_default().into(),
                architecture: name
                    .split("Components-")
                    .nth(1)
                    .and_then(|s| s.split('.').next())
                    .unwrap_or(std::env::consts::ARCH)
                    .into(),
                ..Default::default()
            });
        }
    }
}
fn flatpak_files(root: &Path, scope: &str, files: &mut Vec<Input>) {
    for remote in fs::read_dir(root).into_iter().flatten().flatten() {
        for arch in fs::read_dir(remote.path()).into_iter().flatten().flatten() {
            let active = arch.path().join("active");
            let path = active.join("appstream.xml.gz");
            if path.is_file() {
                files.push(Input {
                    path,
                    backend: "flatpak".into(),
                    repository: remote.file_name().to_string_lossy().into_owned(),
                    scope: scope.into(),
                    architecture: arch.file_name().to_string_lossy().into_owned(),
                    icon_root: active.join("icons"),
                });
            }
        }
    }
}
pub fn load(native: &str) -> Catalog {
    let mut files = Vec::new();
    for root in [
        "/usr/share/swcatalog/xml",
        "/usr/share/swcatalog/yaml",
        "/var/cache/swcatalog/xml",
        "/var/cache/swcatalog/yaml",
        "/usr/share/app-info/xmls",
        "/usr/share/app-info/yaml",
        "/var/cache/app-info/xmls",
        "/var/lib/app-info/yaml",
        "/var/lib/apt/lists",
    ] {
        native_files(Path::new(root), native, &mut files);
    }
    flatpak_files(Path::new("/var/lib/flatpak/appstream"), "system", &mut files);
    for config in fs::read_dir("/etc/flatpak/installations.d").into_iter().flatten().flatten() {
        if config.path().extension().is_none_or(|e| e != "conf") {
            continue;
        }
        let Ok(text) = fs::read_to_string(config.path()) else {
            continue;
        };
        let mut installation = String::new();
        for line in text.lines().map(str::trim) {
            if let Some(name) = line.strip_prefix("[Installation \"").and_then(|v| v.strip_suffix("\"]")) {
                installation = name.to_owned();
            }
            if let Some(path) = line.strip_prefix("Path=").filter(|_| !installation.is_empty()) {
                let root = Path::new(path.trim());
                if root.is_absolute() {
                    flatpak_files(&root.join("appstream"), &format!("installation={installation}"), &mut files);
                }
            }
        }
    }

    if let Some(home) = std::env::var_os("HOME") {
        let data = std::env::var_os("XDG_DATA_HOME")
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from(home).join(".local/share"));
        flatpak_files(&data.join("flatpak/appstream"), "user", &mut files);
    }
    let mut apps = Vec::new();
    let mut warnings = Vec::new();
    let mut seen = HashSet::new();
    for input in files {
        if !seen.insert((fs::canonicalize(&input.path).unwrap_or(input.path.clone()), input.scope.clone())) {
            continue;
        }
        let read = (|| -> Result<String, String> {
            let file = fs::File::open(&input.path).map_err(|e| e.to_string())?;
            let reader: Box<dyn Read> = if input.path.extension().is_some_and(|v| v == "gz") {
                Box::new(flate2::read::GzDecoder::new(file))
            } else {
                Box::new(file)
            };
            let mut text = String::new();
            reader.take(128 * 1024 * 1024).read_to_string(&mut text).map_err(|e| e.to_string())?;
            Ok(text)
        })();
        let parsed = read.and_then(|text| {
            if text.trim_start().starts_with('<') {
                parse_xml(&text, &input)
            } else {
                parse_yaml(&text, &input)
            }
        });
        match parsed {
            Ok(entries) => apps.extend(entries),
            Err(error) => warnings.push(format!("{}: {error}", input.path.display())),
        }
    }
    let mut catalog = Catalog::from_apps(apps);
    catalog.warnings = warnings;
    catalog
}

pub fn package(app: &App, offer: &Offer) -> PackageInfo {
    PackageInfo {
        name: offer.package.clone(),
        description: app.summary.clone(),
        long_description: app.description.clone(),
        homepage_url: app.homepage.clone(),
        repository: if offer.scope.is_empty() {
            offer.repository.clone()
        } else {
            format!("{}:{}", offer.scope, offer.repository)
        },
        architecture: offer.architecture.clone(),
        install_ref: offer.reference.clone(),
        ..Default::default()
    }
}

pub const CATEGORIES: &[(&str, &str)] = &[
    ("", "All apps"),
    ("AudioVideo", "Audio & Video"),
    ("Development", "Development"),
    ("Education", "Education"),
    ("Game", "Games"),
    ("Graphics", "Graphics"),
    ("Network", "Internet"),
    ("Office", "Office"),
    ("Science", "Science"),
    ("System", "System"),
    ("Utility", "Utilities"),
];

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn xml_sources_and_merging() {
        let input = Input {
            backend: "pacman".into(),
            ..Default::default()
        };
        let xml = r#"<components origin="extra"><component type="desktop-application"><id>org.mozilla.firefox.desktop</id><name>Firefox</name><summary>Browser</summary><pkgname>firefox</pkgname><categories><category>Network</category></categories></component></components>"#;
        let mut apps = parse_xml(xml, &input).unwrap();
        let mut flatpak = apps[0].clone();
        flatpak.id = "org.mozilla.firefox".into();
        flatpak.offers = vec![Offer {
            backend: "flatpak".into(),
            package: "org.mozilla.firefox".into(),
            repository: "flathub".into(),
            scope: "user".into(),
            ..Default::default()
        }];
        apps.push(flatpak);
        let catalog = Catalog::from_apps(apps);
        assert_eq!(catalog.apps.len(), 1);
        assert_eq!(catalog.find("firefox-bin").unwrap().offers.len(), 2);
    }
    #[test]
    fn dep11_documents() {
        let yaml = "---\nFile: DEP-11\nOrigin: debian-main\nMediaBaseUrl: https://example.org/media\n---\nType: desktop-application\nID: org.example.App\nPackage: example\nName:\n  C: Example\nSummary:\n  C: Demo\nCategories: [Utility]\nScreenshots:\n- source-image:\n    url: app/image.png\n";
        let apps = parse_yaml(
            yaml,
            &Input {
                backend: "apt".into(),
                ..Default::default()
            },
        )
        .unwrap();
        assert_eq!(apps.len(), 1);
        assert_eq!(apps[0].screenshots, ["https://example.org/media/app/image.png"]);
        assert_eq!(apps[0].offers[0].repository, "debian-main");
    }
    #[test]
    fn transitive_aliases_merge_but_display_titles_do_not() {
        let make = |id: &str, aliases: Vec<String>| App {
            id: id.into(),
            name: "Shared title".into(),
            aliases,
            offers: vec![Offer {
                backend: "flatpak".into(),
                package: id.into(),
                ..Default::default()
            }],
            ..Default::default()
        };
        let a = make("org.first.App", vec!["first.desktop".into()]);
        let b = make("org.second.App", vec!["second.desktop".into()]);
        assert_eq!(Catalog::from_apps(vec![a.clone(), b.clone()]).apps.len(), 2);
        let bridge = make("org.bridge.App", vec!["first.desktop".into(), "second.desktop".into()]);
        let catalog = Catalog::from_apps(vec![a, b, bridge]);
        assert_eq!(catalog.apps.len(), 1);
        assert_eq!(catalog.apps[0].offers.len(), 3);
    }
    #[test]
    fn flatpak_scope_and_full_reference_survive_parsing() {
        let input = Input {
            backend: "flatpak".into(),
            repository: "flathub-beta".into(),
            scope: "user".into(),
            ..Default::default()
        };
        let xml = r#"<components><component type="desktop-application"><id>org.example.App</id><name>Example</name><bundle type="flatpak">app/org.example.App/aarch64/beta</bundle></component></components>"#;
        let apps = parse_xml(xml, &input).unwrap();
        let offer = &apps[0].offers[0];
        assert_eq!(offer.architecture, "aarch64");
        let package = package(&apps[0], offer);
        assert_eq!(package.install_ref, "app/org.example.App/aarch64/beta");
        assert_eq!(package.repository, "user:flathub-beta");
    }
    #[test]
    fn malformed_catalogs_and_unsafe_icon_paths() {
        assert!(parse_xml("<broken>", &Input::default()).is_err());
        assert!(parse_yaml("Type: [broken", &Input::default()).is_err());
        assert!(icon_path(&Input::default(), "../../outside", "../../secret.png").is_empty());
        assert_eq!(plain_markup("<p>Hello <em>world</em>.</p>"), "Hello world.");
    }
}
