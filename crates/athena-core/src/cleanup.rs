//! Inventories are fixed before confirmation. User cleanup never follows links
//! or crosses mounts and only unlinks unchanged files owned by this user.
use crate::{
    Command,
    process::{self, run},
};
use std::{
    fs,
    os::unix::fs::MetadataExt,
    path::{Path, PathBuf},
    time::{Duration, SystemTime},
};

#[derive(Clone, Debug)]
pub struct File {
    pub path: PathBuf,
    pub bytes: u64,
    device: u64,
    inode: u64,
    modified: SystemTime,
    uid: u32,
}
#[derive(Clone, Debug)]
pub struct Area {
    pub title: String,
    pub files: Vec<File>,
    pub caution: bool,
    pub errors: usize,
}
impl Area {
    pub fn bytes(&self) -> u64 {
        self.files.iter().map(|f| f.bytes).sum()
    }
}
pub fn home() -> Result<PathBuf, String> {
    std::env::var_os("HOME")
        .map(PathBuf::from)
        .filter(|p| p.is_absolute())
        .ok_or("Home directory unavailable".into())
}
fn uid() -> Option<u32> {
    fs::metadata("/proc/self").ok().map(|m| m.uid())
}
fn scan(path: &Path, device: u64, owner: Option<u32>, older: Option<Duration>, area: &mut Area) {
    let Ok(meta) = fs::symlink_metadata(path) else {
        area.errors += 1;
        return;
    };
    if meta.file_type().is_symlink() || meta.dev() != device || owner.is_some_and(|u| u != meta.uid()) {
        return;
    }
    if meta.is_dir() {
        match fs::read_dir(path) {
            Ok(entries) => {
                for entry in entries {
                    match entry {
                        Ok(entry) => scan(&entry.path(), device, owner, older, area),
                        Err(_) => area.errors += 1,
                    }
                }
            }
            Err(_) => area.errors += 1,
        }
    } else if meta.is_file() {
        let Ok(modified) = meta.modified() else {
            return;
        };
        if older.is_some_and(|age| SystemTime::now().duration_since(modified).unwrap_or_default() < age) {
            return;
        }
        area.files.push(File {
            path: path.into(),
            bytes: meta.len(),
            device,
            inode: meta.ino(),
            modified,
            uid: meta.uid(),
        });
    }
}
pub fn inventory(title: &str, path: &Path, owned: bool, older: Option<Duration>, caution: bool) -> Area {
    let mut area = Area {
        title: title.into(),
        files: vec![],
        caution,
        errors: 0,
    };
    if !path.exists() {
        return area;
    }
    if owned && uid().is_none() {
        area.errors += 1;
        return area;
    }
    if let Ok(meta) = fs::symlink_metadata(path) {
        if meta.is_dir() && !meta.file_type().is_symlink() {
            match fs::read_dir(path) {
                Ok(entries) => {
                    for entry in entries {
                        match entry {
                            Ok(entry) => scan(&entry.path(), meta.dev(), if owned { uid() } else { None }, older, &mut area),
                            Err(_) => area.errors += 1,
                        }
                    }
                }
                Err(_) => area.errors += 1,
            }
        } else {
            scan(path, meta.dev(), if owned { uid() } else { None }, older, &mut area);
        }
    }
    area
}
pub fn user_areas() -> Result<Vec<Area>, String> {
    let home = home()?;
    let cache = std::env::var_os("XDG_CACHE_HOME").map(PathBuf::from).unwrap_or_else(|| home.join(".cache"));
    let data = std::env::var_os("XDG_DATA_HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| home.join(".local/share"));
    Ok(vec![
        inventory(
            "Temporary files you own (older than 7 days)",
            Path::new("/tmp"),
            true,
            Some(Duration::from_secs(7 * 86400)),
            false,
        ),
        inventory("Application cache", &cache, true, None, false),
        inventory("Trash (files and metadata)", &data.join("Trash"), true, None, true),
        inventory("Thumbnails", &cache.join("thumbnails"), true, None, true),
        inventory("Bash history", &home.join(".bash_history"), true, None, true),
        inventory("Zsh history", &home.join(".zsh_history"), true, None, true),
        inventory("Fish history", &data.join("fish/fish_history"), true, None, true),
        inventory("Recently used files list", &data.join("recently-used.xbel"), true, None, true),
    ])
}
pub fn delete(area: &Area) -> Result<usize, String> {
    let mut deleted = 0;
    for file in &area.files {
        let relative = file.path.strip_prefix("/").map_err(|e| e.to_string())?;
        let parent = crate::safe_path::parent(Path::new("/"), relative, false)?;
        let opened = crate::safe_path::read(Path::new("/"), relative)?;
        let meta = opened.metadata().map_err(|e| e.to_string())?;
        if !meta.is_file()
            || meta.file_type().is_symlink()
            || meta.uid() != file.uid
            || Some(meta.uid()) != uid()
            || meta.dev() != file.device
            || meta.ino() != file.inode
            || meta.len() != file.bytes
            || meta.modified().ok() != Some(file.modified)
        {
            return Err(format!("File changed since preview: {}. Refresh the preview.", file.path.display()));
        }
        let current = rustix::fs::statat(
            &parent,
            file.path.file_name().ok_or("Invalid file name")?,
            rustix::fs::AtFlags::SYMLINK_NOFOLLOW,
        )
        .map_err(|e| e.to_string())?;
        if current.st_ino != file.inode || current.st_dev != file.device {
            return Err("File changed since preview".into());
        }
        rustix::fs::unlinkat(&parent, file.path.file_name().ok_or("Invalid file name")?, rustix::fs::AtFlags::empty()).map_err(|e| e.to_string())?;
        deleted += 1;
    }
    Ok(deleted)
}
pub fn cache(key: &str) -> Option<(Area, Vec<Command>)> {
    let (path, commands) = match key {
        "pacman" => (
            "/var/cache/pacman/pkg",
            if process::executable_exists("paccache") {
                vec![Command::privileged(["paccache", "-r"])]
            } else {
                vec![Command::privileged(["pacman", "-Sc", "--noconfirm"])]
            },
        ),
        "apt" => ("/var/cache/apt/archives", vec![Command::privileged(["apt-get", "clean"])]),
        "dnf" => ("/var/cache", vec![Command::privileged([dnf_program(), "clean", "all"])]),
        _ => return None,
    };
    let area = if key == "dnf" {
        let mut area = Area {
            title: "DNF cache".into(),
            files: vec![],
            caution: false,
            errors: 0,
        };
        for name in ["dnf", "libdnf5"] {
            let item = inventory("", &Path::new(path).join(name), false, None, false);
            area.files.extend(item.files);
            area.errors += item.errors;
        }
        area
    } else {
        inventory("Package cache", Path::new(path), false, None, false)
    };
    Some((area, commands))
}
fn flatpak_scopes() -> Vec<String> {
    let mut scopes = vec!["--system".into(), "--user".into()];
    let out = run("flatpak", &["list", "--all", "--columns=installation"], process::SHORT);
    for name in out.stdout.lines().map(str::trim) {
        if ["system", "user"].contains(&name) || name.is_empty() {
            continue;
        }
        if name.bytes().all(|b| b.is_ascii_alphanumeric() || b"._-".contains(&b)) {
            let flag = format!("--installation={name}");
            if !scopes.contains(&flag) {
                scopes.push(flag);
            }
        }
    }
    scopes
}
pub fn unused_preview(key: &str) -> String {
    if key == "pacman" {
        let names = run("pacman", &["-Qdtq"], process::SHORT);
        let names: Vec<_> = names.stdout.lines().collect();
        if names.is_empty() {
            return "No unused dependencies reported.".into();
        }
        let mut args = vec!["-Qi"];
        args.extend(names);
        return run("pacman", &args, process::SHORT).combined();
    }
    if key == "flatpak" {
        // Flatpak has no --assumeno option. Explicitly decline the proposed
        // uninstall; never use --noninteractive here (it implies yes).
        let mut preview = String::new();
        for scope in flatpak_scopes() {
            let out = process::run_with_stdin("flatpak", &["uninstall", "--unused", scope.as_str()], b"n\n", process::LONG);
            preview.push_str(&format!("{scope} (declined preview):\n{}\n", out.combined()));
        }
        let sizes = run("flatpak", &["list", "--runtime", "--columns=ref,size,installation"], process::SHORT);
        preview.push_str(&format!("\nInstalled runtime sizes (includes runtimes still in use):\n{}", sizes.stdout));
        return preview;
    }
    let (program, args): (&str, Vec<&str>) = match key {
        "pacman" => ("pacman", vec!["-Qdt"]),
        "apt" => ("apt-get", vec!["--simulate", "autoremove"]),
        "dnf" => (dnf_program(), vec!["autoremove", "--assumeno"]),
        _ => return "This source does not report unused dependencies.".into(),
    };
    let out = run(program, &args, process::LONG);
    if out.combined().trim().is_empty() {
        "No unused dependencies reported.".into()
    } else {
        out.combined()
    }
}
pub fn size(bytes: u64) -> String {
    if bytes >= 1 << 30 {
        format!("{:.1} GiB", bytes as f64 / (1u64 << 30) as f64)
    } else if bytes >= 1 << 20 {
        format!("{:.1} MiB", bytes as f64 / (1u64 << 20) as f64)
    } else {
        format!("{:.1} KiB", bytes as f64 / 1024.)
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn inventory_does_not_follow_links_and_refuses_changed_files() {
        let root = std::env::temp_dir().join(format!("athena-clean-{}", std::process::id()));
        fs::create_dir_all(&root).unwrap();
        fs::write(root.join("file"), "one").unwrap();
        std::os::unix::fs::symlink("/etc", root.join("link")).unwrap();
        let area = inventory("test", &root, true, None, false);
        assert_eq!(area.files.len(), 1);
        assert_eq!(area.bytes(), 3);
        fs::write(root.join("file"), "changed").unwrap();
        assert!(delete(&area).is_err());
        let area = inventory("test", &root, true, None, false);
        assert_eq!(delete(&area).unwrap(), 1);
        fs::remove_dir_all(root).unwrap();
    }
}

#[cfg(test)]
mod changed_parent {
    use super::*;
    #[test]
    fn replacing_a_parent_with_a_link_never_deletes_its_target() {
        let root = std::env::temp_dir().join(format!("athena-clean-parent-{}", std::process::id()));
        let cache = root.join("cache");
        let outside = root.join("outside");
        fs::create_dir_all(&cache).unwrap();
        fs::create_dir_all(&outside).unwrap();
        fs::write(cache.join("file"), "cache").unwrap();
        fs::write(outside.join("file"), "keep").unwrap();
        let approved = inventory("test", &cache, true, None, false);
        fs::rename(&cache, root.join("old")).unwrap();
        std::os::unix::fs::symlink(&outside, &cache).unwrap();
        assert!(delete(&approved).is_err());
        assert_eq!(fs::read_to_string(outside.join("file")).unwrap(), "keep");
        fs::remove_dir_all(root).unwrap();
    }
}

pub(crate) fn dnf_program() -> &'static str {
    if process::executable_exists("dnf5") { "dnf5" } else { "dnf" }
}
