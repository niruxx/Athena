//! Release checks are optional and never block app startup.
use crate::process;

/// Only offer a release hosted by this project's GitHub repository.
pub fn newer_release(json: &str, current: &str) -> Option<(String, String)> {
    let data: serde_json::Value = serde_json::from_str(json).ok()?;
    if data.get("draft").and_then(|v| v.as_bool()).unwrap_or(false) { return None; }
    let tag = data.get("tag_name")?.as_str()?;
    let version = semver::Version::parse(tag.trim_start_matches('v')).ok()?;
    let current = semver::Version::parse(current).ok()?;
    let url = data.get("html_url")?.as_str()?;
    if version <= current || !url.starts_with("https://github.com/niruxx/Athena/releases/tag/") { return None; }
    Some((tag.to_owned(), url.to_owned()))
}

pub fn check() -> Option<(String, String)> {
    let out = process::run("curl", &["--fail", "--silent", "--show-error", "--location", "--proto", "=https", "--proto-redir", "=https", "--max-time", "15", "--max-filesize", "1048576", "--header", "Accept: application/vnd.github+json", "https://api.github.com/repos/niruxx/Athena/releases/latest"], std::time::Duration::from_secs(20));
    if !out.success() { return None; }
    newer_release(&out.stdout, crate::VERSION)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn versions_and_origins() {
        let release = r#"{"tag_name":"v4.0.0","html_url":"https://github.com/niruxx/Athena/releases/tag/v4.0.0"}"#;
        assert!(newer_release(release, "4.0.0-alpha.1").is_some());
        assert!(newer_release(release, "4.0.0").is_none());
        assert!(newer_release(release, "5.0.0").is_none());
        assert!(newer_release(&release.replace("github.com", "example.com"), "3.0.0").is_none());
        assert!(newer_release("bad json", "3.0.0").is_none());
    }
}
