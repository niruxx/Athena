//! Arch news and mirror ranking previews. Saving is a separate privileged step.
use crate::{Command, process};
use std::{fs, path::Path};
#[derive(Clone, Debug)]
pub struct News {
    pub id: String,
    pub title: String,
    pub url: String,
    pub date: String,
    pub description: String,
}
/// Where a distribution publishes its news.
#[derive(Debug, Clone)]
pub struct NewsFeed {
    pub name: &'static str,
    pub site: &'static str,
    pub url: &'static str,
    /// Links outside this prefix are ignored (the Arch feed also carries other items).
    pub link_prefix: &'static str,
}

/// The news feed for this system's distribution, when it has a known one.
pub fn news_feed() -> Option<NewsFeed> {
    let release = fs::read_to_string("/etc/os-release").unwrap_or_default();
    let id = release.lines().find_map(|l| l.strip_prefix("ID=")).unwrap_or_default().trim_matches('"').to_owned();
    if id == "ubuntu" || release.contains("ID_LIKE=ubuntu") || release.contains("ID_LIKE=\"ubuntu") {
        return Some(NewsFeed { name: "Ubuntu News", site: "ubuntu.com", url: "https://ubuntu.com/blog/feed", link_prefix: "https://ubuntu.com/" });
    }
    match crate::distro::family_from_os_release(&release) {
        crate::distro::DistroFamily::Arch => Some(NewsFeed { name: "Arch Linux News", site: "archlinux.org", url: "https://archlinux.org/feeds/news/", link_prefix: "https://archlinux.org/news/" }),
        crate::distro::DistroFamily::Fedora => Some(NewsFeed { name: "Fedora News", site: "fedoramagazine.org", url: "https://fedoramagazine.org/feed/", link_prefix: "https://fedoramagazine.org/" }),
        crate::distro::DistroFamily::Debian => Some(NewsFeed { name: "Debian News", site: "debian.org", url: "https://www.debian.org/News/news", link_prefix: "https://www.debian.org/" }),
        crate::distro::DistroFamily::Unknown => None,
    }
}

pub fn parse_news(xml: &str) -> Result<Vec<News>, String> {
    parse_feed(xml, "https://archlinux.org/news/")
}

pub fn parse_feed(xml: &str, link_prefix: &str) -> Result<Vec<News>, String> {
    let doc = roxmltree::Document::parse(xml).map_err(|e| e.to_string())?;
    let mut news = Vec::new();
    let tags = regex::Regex::new("<[^>]*>").unwrap();
    for item in doc.descendants().filter(|n| n.has_tag_name("item")) {
        let field = |tag| {
            item.children()
                .find(|n| n.has_tag_name(tag))
                .and_then(|n| n.text())
                .unwrap_or_default()
                .to_owned()
        };
        let url = field("link");
        if !url.starts_with(link_prefix) {
            continue;
        }
        let id = field("guid");
        news.push(News {
            id: if id.is_empty() { url.clone() } else { id },
            title: field("title"),
            url,
            date: { let d = field("pubDate"); if d.is_empty() { field("date") } else { d } },
            description: tags.replace_all(&field("description"), "").into_owned(),
        });
        if news.len() == 20 {
            break;
        }
    }
    Ok(news)
}
pub fn news() -> Result<Vec<News>, String> {
    fetch_news(&NewsFeed { name: "Arch Linux News", site: "archlinux.org", url: "https://archlinux.org/feeds/news/", link_prefix: "https://archlinux.org/news/" })
}

/// Fetches and parses one distribution's feed.
pub fn fetch_news(feed: &NewsFeed) -> Result<Vec<News>, String> {
    let output = process::run(
        "curl",
        &[
            "--fail",
            "--silent",
            "--show-error",
            "--location",
            "--proto",
            "=https",
            "--proto-redir",
            "=https",
            "--max-time",
            "20",
            "--max-filesize",
            "4194304",
            feed.url,
        ],
        std::time::Duration::from_secs(25),
    );
    if output.success() {
        parse_feed(&output.stdout, feed.link_prefix)
    } else {
        Err(output.combined())
    }
}
pub fn rank_mirrors(destination: &Path) -> Result<String, String> {
    let path = destination.to_str().ok_or("Invalid preview path")?;
    let release = fs::read_to_string("/etc/os-release").map_err(|e| e.to_string())?;
    let id = release.lines().find_map(|l| l.strip_prefix("ID=")).unwrap_or_default().trim_matches('"');
    let target = match id {
        "manjaro" => "manjaro",
        "artix" => "artix",
        "arch" | "endeavouros" | "garuda" | "cachyos" | "arcolinux" | "archcraft" => "arch",
        _ => return Err("Mirror ranking is not configured for this distribution. Keep its existing mirror list.".into()),
    };
    let command = if target == "arch" && process::executable_exists("reflector") {
        Command::new("reflector", ["--latest", "20", "--protocol", "https", "--sort", "rate", "--save", path])
    } else if process::executable_exists("rate-mirrors") {
        Command::new("rate-mirrors", ["--save", path, target])
    } else {
        return Err("Install reflector or rate-mirrors first.".into());
    };
    let result = process::run_sequence(&[command], process::LONG);
    if !result.success() {
        return Err(result.combined());
    }
    let text = fs::read_to_string(destination).map_err(|e| e.to_string())?;
    if !text.lines().any(|line| line.trim_start().starts_with("Server = https://")) {
        return Err("Ranking returned no HTTPS mirror servers.".into());
    }
    Ok(text)
}
pub fn mirror_save_commands(path: &Path) -> Vec<Command> {
    let timestamp = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_secs();
    vec![
        Command::privileged([
            "cp",
            "--",
            "/etc/pacman.d/mirrorlist",
            &format!("/etc/pacman.d/mirrorlist.athena-{timestamp}.bak"),
        ]),
        Command::privileged(["install", "-m644", "--", &path.to_string_lossy(), "/etc/pacman.d/mirrorlist"]),
    ]
}
pub fn fastest_mirror() -> Command {
    if process::executable_exists("dnf5") {
        Command::privileged(["dnf5", "config-manager", "setopt", "fastestmirror=True"])
    } else {
        Command::privileged(["dnf", "config-manager", "--save", "--setopt=fastestmirror=True"])
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn news_only_links_to_arch() {
        let xml = r#"<rss><channel><item><title>Upgrade notice</title><guid>1</guid><link>https://archlinux.org/news/example/</link><description>&lt;p&gt;Read first&lt;/p&gt;</description></item><item><link>https://example.org/news/</link></item></channel></rss>"#;
        let items = parse_news(xml).unwrap();
        assert_eq!(items.len(), 1);
        assert_eq!(items[0].description, "Read first");
        assert!(parse_news("broken").is_err());
    }
}
