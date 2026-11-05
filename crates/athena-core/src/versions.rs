//! Exact pacman cache and Arch Linux Archive package candidates.
use crate::{Command, process};
use std::path::Path;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Version {
    pub version: String,
    pub architecture: String,
    pub location: String,
}

pub fn valid_name(name: &str) -> bool {
    !name.is_empty() && !name.starts_with(['-', '.']) && name.bytes().all(|b| b.is_ascii_alphanumeric() || b"@._+-".contains(&b))
}

pub fn from_filename(package: &str, filename: &str, location: String) -> Option<Version> {
    if !valid_name(package) || filename.contains(['/', '\\']) {
        return None;
    }
    let suffix = filename.strip_prefix(&format!("{package}-"))?;
    let suffix = suffix
        .strip_suffix(".pkg.tar.zst")
        .or_else(|| suffix.strip_suffix(".pkg.tar.xz"))
        .or_else(|| suffix.strip_suffix(".pkg.tar.gz"))?;
    let parts: Vec<_> = suffix.split('-').collect();
    if parts.len() != 3 || parts.iter().any(|p| p.is_empty()) {
        return None;
    }
    Some(Version {
        version: format!("{}-{}", parts[0], parts[1]),
        architecture: parts[2].into(),
        location,
    })
}

pub fn cached(package: &str) -> Vec<Version> {
    if !valid_name(package) {
        return Vec::new();
    }
    let out = process::run("pacman-conf", &["CacheDir"], process::SHORT);
    let roots: Vec<_> = if out.success() {
        out.stdout.lines().map(str::to_owned).collect()
    } else {
        vec!["/var/cache/pacman/pkg".into()]
    };
    roots
        .iter()
        .flat_map(|root| std::fs::read_dir(root).into_iter().flatten().flatten())
        .filter_map(|entry| {
            let filename = entry.file_name();
            from_filename(package, filename.to_str()?, entry.path().to_string_lossy().into_owned())
        })
        .collect()
}

pub fn parse_archive(package: &str, html: &str) -> Vec<Version> {
    if !valid_name(package) {
        return Vec::new();
    }
    let base = format!("https://archive.archlinux.org/packages/{}/{package}/", &package[..1]);
    let re = regex::Regex::new(r#"href="([A-Za-z0-9@._+%\-]+)""#).unwrap();
    re.captures_iter(html)
        .filter_map(|m| from_filename(package, &m[1], format!("{base}{}", &m[1])))
        .collect()
}

pub fn archive(package: &str) -> Vec<Version> {
    if !valid_name(package) {
        return Vec::new();
    }
    let url = format!("https://archive.archlinux.org/packages/{}/{package}/", &package[..1]);
    let out = process::run(
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
            "4194304",
            &url,
        ],
        std::time::Duration::from_secs(25),
    );
    if out.success() { parse_archive(package, &out.stdout) } else { Vec::new() }
}

pub fn install(candidate: &Version) -> Option<Command> {
    if !candidate.location.starts_with("https://archive.archlinux.org/packages/") && !Path::new(&candidate.location).is_absolute() {
        return None;
    }
    Some(Command::privileged(["pacman", "-U", "--noconfirm", "--", &candidate.location]))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn exact_package_only() {
        assert!(from_filename("foo", "foo-bar-1-1-x86_64.pkg.tar.zst", String::new()).is_none());
        let versions = parse_archive(
            "foo",
            r#"<a href="foo-1.2-3-x86_64.pkg.tar.zst">package</a><a href="foo-1.2-3-x86_64.pkg.tar.zst.sig">sig</a><a href="../foo-1-1-any.pkg.tar.zst">bad</a>"#,
        );
        assert_eq!(versions.len(), 1);
        assert_eq!(versions[0].version, "1.2-3");
        assert!(parse_archive("../foo", "").is_empty());
        assert!(!valid_name("-foo"));
    }
}
