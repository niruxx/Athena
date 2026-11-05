//! Small, read-only metadata lookup for Details. The full catalog is Phase 3.
use crate::process;
use std::{
    fs,
    hash::{Hash, Hasher},
    io::Read,
    path::{Path, PathBuf},
};

#[derive(Debug, Default, Clone)]
pub struct AppMetadata {
    pub id: String,
    pub packages: Vec<String>,
    pub screenshots: Vec<String>,
    pub description: String,
    pub homepage: String,
}

pub fn parse_metadata(xml: &str, name: &str) -> Option<AppMetadata> {
    let doc = roxmltree::Document::parse(xml).ok()?;
    for component in doc.descendants().filter(|n| n.has_tag_name("component")) {
        let field = |tag| component.children().find(|n| n.has_tag_name(tag)).and_then(|n| n.text()).unwrap_or("");
        let id = field("id").trim_end_matches(".desktop");
        let packages: Vec<String> = component
            .children()
            .filter(|n| n.has_tag_name("pkgname"))
            .filter_map(|n| n.text().map(str::to_owned))
            .collect();
        let launch = field("launchable").trim_end_matches(".desktop");
        if id != name && launch != name && !packages.iter().any(|p| p == name) {
            continue;
        }
        let screenshots = component
            .descendants()
            .filter(|n| n.has_tag_name("image") && n.attribute("type") != Some("thumbnail"))
            .filter_map(|n| n.text())
            .filter(|s| s.starts_with("https://"))
            .take(5)
            .map(str::to_owned)
            .collect();
        let description = component
            .children()
            .find(|n| n.has_tag_name("description") && n.attribute(("http://www.w3.org/XML/1998/namespace", "lang")).is_none())
            .map(|n| {
                n.descendants()
                    .filter(|n| n.has_tag_name("p") || n.has_tag_name("li"))
                    .map(|n| n.descendants().filter(|n| n.is_text()).filter_map(|n| n.text()).collect::<String>())
                    .collect::<Vec<_>>()
                    .join("\n\n")
            })
            .unwrap_or_default();
        let homepage = component
            .children()
            .find(|n| n.has_tag_name("url") && n.attribute("type") == Some("homepage"))
            .and_then(|n| n.text())
            .unwrap_or_default()
            .to_owned();
        return Some(AppMetadata {
            id: id.to_owned(),
            packages,
            screenshots,
            description,
            homepage,
        });
    }
    None
}

fn collect(dir: &Path, depth: usize, files: &mut Vec<PathBuf>) {
    let Ok(entries) = fs::read_dir(dir) else { return };
    for entry in entries.flatten() {
        let path = entry.path();
        let name = entry.file_name().to_string_lossy().into_owned();
        if path.is_dir() && depth > 0 {
            collect(&path, depth - 1, files);
        } else if name.ends_with(".xml") || name.ends_with(".xml.gz") {
            files.push(path);
        }
    }
}

pub fn metadata(name: &str) -> AppMetadata {
    if let Some(metadata) = crate::catalog::current().metadata(name) {
        return metadata;
    }
    static CACHE: std::sync::LazyLock<std::sync::Mutex<std::collections::HashMap<String, AppMetadata>>> =
        std::sync::LazyLock::new(|| std::sync::Mutex::new(std::collections::HashMap::new()));
    if let Some(metadata) = CACHE.lock().unwrap().get(name).cloned() {
        return metadata;
    }
    let metadata = metadata_uncached(name);
    CACHE.lock().unwrap().insert(name.to_owned(), metadata.clone());
    metadata
}

fn metadata_uncached(name: &str) -> AppMetadata {
    let mut files = Vec::new();
    for root in [
        "/usr/share/metainfo",
        "/usr/share/appdata",
        "/usr/share/swcatalog/xml",
        "/var/cache/swcatalog/xml",
        "/var/lib/flatpak/appstream",
    ] {
        collect(Path::new(root), 4, &mut files);
    }
    if let Some(home) = std::env::var_os("HOME") {
        collect(&PathBuf::from(&home).join(".local/share/flatpak/appstream"), 4, &mut files);
        collect(&PathBuf::from(home).join(".local/share/metainfo"), 0, &mut files);
    }
    // Prefer a component's own metadata over a distribution-wide catalog.
    files.sort_by_key(|p| !p.file_name().unwrap_or_default().to_string_lossy().contains(name));
    for path in files {
        let Ok(file) = fs::File::open(&path) else { continue };
        let reader: Box<dyn Read> = if path.extension().is_some_and(|x| x == "gz") {
            Box::new(flate2::read::GzDecoder::new(file))
        } else {
            Box::new(file)
        };
        let mut xml = String::new();
        if reader.take(64 * 1024 * 1024).read_to_string(&mut xml).is_err() {
            continue;
        }
        if let Some(metadata) = parse_metadata(&xml, name) {
            return metadata;
        }
    }
    AppMetadata::default()
}

pub fn cache_image(url: &str, cache: &Path) -> Option<PathBuf> {
    static DOWNLOAD: std::sync::Mutex<()> = std::sync::Mutex::new(());
    let _download = DOWNLOAD.lock().ok()?;
    if !url.starts_with("https://") {
        return None;
    }
    fs::create_dir_all(cache).ok()?;
    let mut hash = std::collections::hash_map::DefaultHasher::new();
    url.hash(&mut hash);
    let path = cache.join(format!("{:016x}.image", hash.finish()));
    if valid_image(&path) {
        return Some(path);
    }
    let temporary = path.with_extension(format!("{}.part", std::process::id()));
    let output = process::run(
        "curl",
        &[
            "--fail",
            "--silent",
            "--location",
            "--proto",
            "=https",
            "--proto-redir",
            "=https",
            "--max-time",
            "20",
            "--max-filesize",
            "10485760",
            "--output",
            temporary.to_str()?,
            url,
        ],
        std::time::Duration::from_secs(25),
    );
    if !output.success() {
        let _ = fs::remove_file(&temporary);
        return None;
    }
    if !valid_image(&temporary) {
        let _ = fs::remove_file(&temporary);
        return None;
    }
    fs::rename(&temporary, &path).ok()?;
    Some(path)
}

fn valid_image(path: &Path) -> bool {
    let Ok(mut file) = fs::File::open(path) else { return false };
    let mut header = [0; 12];
    if file.read_exact(&mut header).is_err() {
        return false;
    }
    is_jxl_header(&header)
        || header.starts_with(b"\x89PNG\r\n\x1a\n")
        || header.starts_with(b"\xff\xd8\xff")
        || (&header[..4] == b"RIFF" && &header[8..] == b"WEBP")
}

fn is_jxl_header(header: &[u8]) -> bool {
    header.starts_with(b"\xff\x0a") || header.starts_with(b"\x00\x00\x00\x0cJXL \r\n\x87\n")
}
pub fn decoded_image(path: &Path, cache: &Path) -> Option<PathBuf> {
    static DECODE: std::sync::Mutex<()> = std::sync::Mutex::new(());
    let _decode = DECODE.lock().ok()?;
    let mut header = [0; 12];
    fs::File::open(path).ok()?.read_exact(&mut header).ok()?;
    if !is_jxl_header(&header) {
        return Some(path.to_owned());
    }
    fs::create_dir_all(cache).ok()?;
    let bytes = fs::read(path).ok()?;
    use sha2::Digest;
    let hash = format!("{:x}", sha2::Sha256::digest(&bytes));
    let output = cache.join(format!("{hash}.png"));
    if valid_image(&output) {
        return Some(output);
    }
    let temporary = output.with_extension(format!("{}.png", std::process::id()));
    let result = process::run("djxl", &[path.to_str()?, temporary.to_str()?, "--quiet"], std::time::Duration::from_secs(20));
    if result.success() && valid_image(&temporary) {
        fs::rename(&temporary, &output).ok()?;
        Some(output)
    } else {
        let _ = fs::remove_file(temporary);
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn matches_exact_identity_and_ignores_thumbnails() {
        let xml = r#"<components><component><id>org.example.App</id><pkgname>example</pkgname><screenshots><screenshot><image type="thumbnail">https://example.org/small.png</image><image type="source">https://example.org/full.png</image><image>http://example.org/insecure.png</image></screenshot></screenshots></component></components>"#;
        let metadata = parse_metadata(xml, "example").unwrap();
        assert_eq!(metadata.id, "org.example.App");
        assert_eq!(metadata.screenshots, ["https://example.org/full.png"]);
        assert!(parse_metadata(xml, "exam").is_none());
        assert!(parse_metadata("broken", "example").is_none());
    }
    #[test]
    fn reads_description_and_homepage() {
        let metadata = parse_metadata(r#"<component><id>app</id><description><p>Hello <em>world</em>.</p><p>Second paragraph.</p></description><url type="homepage">https://example.org</url></component>"#, "app").unwrap();
        assert_eq!(metadata.description, "Hello world.\n\nSecond paragraph.");
        assert_eq!(metadata.homepage, "https://example.org");
    }

    #[test]
    fn reuses_cached_images_and_rejects_insecure_urls() {
        let cache = std::env::temp_dir().join(format!("athena-media-test-{}", std::process::id()));
        fs::create_dir_all(&cache).unwrap();
        let url = "https://example.invalid/screenshot.png";
        let mut hash = std::collections::hash_map::DefaultHasher::new();
        url.hash(&mut hash);
        let path = cache.join(format!("{:016x}.image", hash.finish()));
        fs::write(&path, b"\x89PNG\r\n\x1a\n1234").unwrap();
        assert_eq!(cache_image(url, &cache), Some(path));
        assert!(cache_image("http://example.invalid/image", &cache).is_none());
        fs::remove_dir_all(cache).unwrap();
    }
}
