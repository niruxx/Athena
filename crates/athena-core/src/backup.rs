//! Athena backups contain regular files only, relative to a home/ namespace.
//! Restore previews bind each path to its SHA256; links and special files are refused.
use crate::transfer::Manifest;
use flate2::{Compression, read::GzDecoder, write::GzEncoder};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    fs::{self, File, OpenOptions},
    io::{Read, Write},
    os::unix::fs::{OpenOptionsExt, PermissionsExt},
    path::{Component, Path, PathBuf},
};
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Index {
    pub version: u32,
    pub packages: Manifest,
    pub skipped: Vec<String>,
    pub files: Vec<PathBuf>,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Entry {
    pub path: PathBuf,
    pub bytes: u64,
    pub digest: String,
    pub mode: u32,
    pub overwrite: bool,
}
#[derive(Debug, Clone)]
pub struct Preview {
    pub entries: Vec<Entry>,
    pub index: Index,
}
fn err(e: impl std::fmt::Display) -> String {
    e.to_string()
}
fn relative(path: &Path) -> Result<(), String> {
    if path.as_os_str().is_empty() || !path.components().all(|c| matches!(c, Component::Normal(_))) {
        Err("Backup contains an unsafe path".into())
    } else {
        Ok(())
    }
}
fn digest(reader: &mut dyn Read) -> Result<String, String> {
    let mut hash = Sha256::new();
    let mut buf = [0; 65536];
    loop {
        let n = reader.read(&mut buf).map_err(err)?;
        if n == 0 {
            break;
        }
        hash.update(&buf[..n]);
    }
    Ok(format!("{:x}", hash.finalize()))
}
pub fn candidates(home: &Path) -> Result<Vec<PathBuf>, String> {
    let mut paths = vec![];
    for entry in fs::read_dir(home).map_err(err)? {
        let entry = entry.map_err(err)?;
        let name = entry.file_name();
        let name = name.to_string_lossy();
        let meta = entry.file_type().map_err(err)?;
        if name.starts_with('.') && (meta.is_file() || meta.is_dir()) && ![".cache", ".var"].contains(&name.as_ref()) && !name.ends_with("_history") {
            paths.push(PathBuf::from(name.as_ref()));
        }
    }
    if home.join(".var/app").is_dir() {
        paths.push(".var/app".into());
    }
    paths.sort();
    Ok(paths)
}
fn gather(home: &Path, path: &Path, files: &mut Vec<PathBuf>, skipped: &mut Vec<String>) -> Result<(), String> {
    let meta = fs::symlink_metadata(home.join(path)).map_err(err)?;
    if meta.file_type().is_symlink() || !(meta.is_file() || meta.is_dir()) {
        skipped.push(path.display().to_string());
        return Ok(());
    }
    if meta.is_dir() {
        for entry in fs::read_dir(home.join(path)).map_err(err)? {
            gather(home, &path.join(entry.map_err(err)?.file_name()), files, skipped)?;
        }
    } else {
        files.push(path.to_owned());
    }
    Ok(())
}
fn parent_safe(root: &Path, path: &Path, create: bool) -> Result<(), String> {
    relative(path)?;
    let mut current = root.to_owned();
    for component in path.parent().unwrap_or(Path::new("")).components() {
        current.push(component);
        if create && !current.exists() {
            fs::create_dir(&current).map_err(err)?;
            fs::set_permissions(&current, fs::Permissions::from_mode(0o700)).map_err(err)?;
        }
        let meta = fs::symlink_metadata(&current).map_err(err)?;
        if !meta.is_dir() || meta.file_type().is_symlink() {
            return Err(format!("Unsafe parent: {}", current.display()));
        }
    }
    Ok(())
}
fn new_file(path: &Path) -> Result<File, String> {
    OpenOptions::new().write(true).create_new(true).mode(0o600).open(path).map_err(err)
}
pub fn create(home: &Path, selected: &[PathBuf], destination: &Path, compressed: bool, packages: Manifest) -> Result<Index, String> {
    if destination.exists() {
        return Err("Choose a new backup path; existing backups are never overwritten.".into());
    }
    let canonical_home = fs::canonicalize(home).map_err(err)?;
    let parent = fs::canonicalize(destination.parent().ok_or("Backup needs a parent folder")?).map_err(err)?;
    let destination = parent.join(destination.file_name().ok_or("Backup needs a file name")?);
    let mut files = vec![];
    let mut skipped = vec![];
    for path in selected {
        relative(path)?;
        parent_safe(&canonical_home, path, false)?;
        if destination.starts_with(canonical_home.join(path)) {
            return Err("Save the backup outside the selected folders.".into());
        }
        gather(&canonical_home, path, &mut files, &mut skipped)?;
    }
    files.sort();
    files.dedup();
    let index = Index {
        version: 1,
        packages,
        skipped,
        files: files.clone(),
    };
    let json = serde_json::to_vec_pretty(&index).map_err(err)?;
    if compressed {
        let file = new_file(&destination)?;
        let encoder = GzEncoder::new(file, Compression::default());
        let mut archive = tar::Builder::new(encoder);
        archive.follow_symlinks(false);
        let mut header = tar::Header::new_gnu();
        header.set_size(json.len() as u64);
        header.set_mode(0o600);
        header.set_cksum();
        archive.append_data(&mut header, "athena-backup.json", &json[..]).map_err(err)?;
        for path in files {
            parent_safe(&canonical_home, &path, false)?;
            let mut file = crate::safe_path::read(&canonical_home, &path)?; // O_NOFOLLOW on Linux
            let meta = file.metadata().map_err(err)?;
            if !meta.is_file() {
                return Err("Backup input changed".into());
            }
            let mut header = tar::Header::new_gnu();
            header.set_size(meta.len());
            header.set_mode(meta.permissions().mode() & 0o777);
            header.set_cksum();
            archive.append_data(&mut header, Path::new("home").join(path), &mut file).map_err(err)?;
        }
        archive.into_inner().map_err(err)?.finish().map_err(err)?.sync_all().map_err(err)?;
    } else {
        fs::create_dir(&destination).map_err(err)?;
        fs::set_permissions(&destination, fs::Permissions::from_mode(0o700)).map_err(err)?;
        fs::write(destination.join("athena-backup.json"), json).map_err(err)?;
        fs::create_dir(destination.join("home")).map_err(err)?;
        for path in files {
            parent_safe(&canonical_home, &path, false)?;
            parent_safe(&destination.join("home"), &path, true)?;
            let mut input = crate::safe_path::read(&canonical_home, &path)?;
            if !input.metadata().map_err(err)?.is_file() {
                return Err("Backup input changed".into());
            }
            let mut out = new_file(&destination.join("home").join(&path))?;
            std::io::copy(&mut input, &mut out).map_err(err)?;
            fs::set_permissions(
                destination.join("home").join(&path),
                fs::Permissions::from_mode(input.metadata().map_err(err)?.permissions().mode() & 0o777),
            )
            .map_err(err)?;
        }
    }
    Ok(index)
}
fn visit(path: &Path, mut callback: impl FnMut(PathBuf, u64, u32, &mut dyn Read) -> Result<(), String>) -> Result<Index, String> {
    if path.is_dir() {
        let index_path = path.join("athena-backup.json");
        if fs::symlink_metadata(&index_path).map_err(err)?.file_type().is_symlink() {
            return Err("Backup index is a link".into());
        }
        if fs::metadata(&index_path).map_err(err)?.len() > 16 * 1024 * 1024 {
            return Err("Oversized backup index".into());
        }
        let index: Index = serde_json::from_reader(crate::safe_path::read(path, Path::new("athena-backup.json"))?).map_err(err)?;
        let mut files = vec![];
        let mut skipped = vec![];
        let root = path.join("home");
        if fs::symlink_metadata(&root).map_err(err)?.file_type().is_symlink() {
            return Err("Backup home is a link".into());
        }
        for entry in fs::read_dir(&root).map_err(err)? {
            gather(&root, &PathBuf::from(entry.map_err(err)?.file_name()), &mut files, &mut skipped)?;
        }
        if !skipped.is_empty() {
            return Err("Backup contains links or special files".into());
        }
        for path in files {
            parent_safe(&root, &path, false)?;
            let mut file = crate::safe_path::read(&root, &path)?;
            let meta = file.metadata().map_err(err)?;
            callback(path, meta.len(), meta.permissions().mode(), &mut file)?;
        }
        Ok(index)
    } else {
        let mut archive = tar::Archive::new(GzDecoder::new(File::open(path).map_err(err)?));
        let mut index = None;
        for entry in archive.entries().map_err(err)? {
            let mut entry = entry.map_err(err)?;
            let path = entry.path().map_err(err)?.into_owned();
            relative(&path)?;
            if !entry.header().entry_type().is_file() {
                return Err("Backup contains a link, directory or special entry".into());
            }
            if path == Path::new("athena-backup.json") {
                if index.is_some() || entry.size() > 16 * 1024 * 1024 {
                    return Err("Invalid backup index".into());
                }
                index = Some(serde_json::from_reader(&mut entry).map_err(err)?);
            } else {
                let relative_path = path.strip_prefix("home").map_err(err)?.to_owned();
                relative(&relative_path)?;
                callback(relative_path, entry.size(), entry.header().mode().map_err(err)?, &mut entry)?;
            }
        }
        index.ok_or("Missing Athena backup index".into())
    }
}
pub fn preview(backup: &Path, home: &Path) -> Result<Preview, String> {
    let mut entries = vec![];
    let mut seen = std::collections::HashSet::new();
    let index = visit(backup, |path, bytes, mode, reader| {
        if !seen.insert(path.clone()) {
            return Err("Backup contains duplicate paths".into());
        }
        let target = home.join(&path);
        // Existing parent links are always rejected, including links outside home.
        let mut ancestor = target.parent();
        while let Some(parent) = ancestor {
            if parent == home {
                break;
            }
            if parent.exists() && fs::symlink_metadata(parent).map_err(err)?.file_type().is_symlink() {
                return Err(format!("Restore parent is a link: {}", parent.display()));
            }
            ancestor = parent.parent();
        }
        let overwrite = match fs::symlink_metadata(&target) {
            Ok(meta) if meta.is_file() && !meta.file_type().is_symlink() => true,
            Ok(_) => return Err(format!("Restore target is not a regular file: {}", target.display())),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => false,
            Err(e) => return Err(err(e)),
        };
        entries.push(Entry {
            path,
            bytes,
            digest: digest(reader)?,
            mode: mode & 0o777,
            overwrite,
        });
        Ok(())
    })?;
    if index.version != 1 {
        return Err("Unsupported backup version".into());
    }
    let mut expected = index.files.clone();
    for path in &expected {
        relative(path)?;
    }
    expected.sort();
    let mut actual = entries.iter().map(|e| e.path.clone()).collect::<Vec<_>>();
    actual.sort();
    if actual != expected {
        return Err("Backup is incomplete or its file list changed".into());
    }
    // Also validate package metadata before accepting a backup.
    index.packages.json()?;
    entries.sort_by(|a, b| a.path.cmp(&b.path));
    Ok(Preview { entries, index })
}
pub fn restore(backup: &Path, home: &Path, approved: &Preview) -> Result<usize, String> {
    if preview(backup, home)?.entries != approved.entries {
        return Err("Backup or destination changed since preview. Preview again.".into());
    }
    let mut count = 0;
    visit(backup, |path, bytes, mode, reader| {
        let expected = approved.entries.iter().find(|e| e.path == path).ok_or("Unapproved backup path")?;
        if expected.bytes != bytes || expected.mode != mode & 0o777 {
            return Err("Backup changed".into());
        }
        let parent = crate::safe_path::parent(home, &path, true)?;
        let target = home.join(&path);
        if fs::symlink_metadata(&target).is_ok_and(|m| !m.is_file() || m.file_type().is_symlink()) {
            return Err("Restore target changed".into());
        }
        let temporary = format!(".athena-restore-{}-{}", std::process::id(), count);
        let result = (|| {
            let fd = rustix::fs::openat(
                &parent,
                temporary.as_str(),
                rustix::fs::OFlags::WRONLY | rustix::fs::OFlags::CREATE | rustix::fs::OFlags::EXCL | rustix::fs::OFlags::NOFOLLOW | rustix::fs::OFlags::CLOEXEC,
                rustix::fs::Mode::RUSR | rustix::fs::Mode::WUSR,
            )
            .map_err(err)?;
            let mut output = File::from(fd);
            let mut hash = Sha256::new();
            let mut buf = [0; 65536];
            let mut written = 0;
            loop {
                let n = reader.read(&mut buf).map_err(err)?;
                if n == 0 {
                    break;
                }
                hash.update(&buf[..n]);
                output.write_all(&buf[..n]).map_err(err)?;
                written += n as u64;
            }
            if written != bytes || format!("{:x}", hash.finalize()) != expected.digest {
                return Err("Backup changed during restore".into());
            }
            output.sync_all().map_err(err)?;
            output.set_permissions(fs::Permissions::from_mode(mode & 0o777)).map_err(err)?;
            rustix::fs::renameat(&parent, temporary.as_str(), &parent, path.file_name().ok_or("Invalid restore file")?).map_err(err)
        })();
        if result.is_err() {
            let _ = rustix::fs::unlinkat(&parent, temporary.as_str(), rustix::fs::AtFlags::empty());
        }
        result?;
        count += 1;
        Ok(())
    })?;
    Ok(count)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn both_formats_roundtrip_and_protect_restore_links() {
        let root = std::env::temp_dir().join(format!("athena-backup-{}", std::process::id()));
        let home = root.join("source");
        let target = root.join("target");
        fs::create_dir_all(home.join(".config/app")).unwrap();
        fs::create_dir_all(&target).unwrap();
        fs::write(home.join(".config/app/settings"), "original").unwrap();
        for compressed in [false, true] {
            let path = root.join(if compressed { "archive.gz" } else { "plain" });
            create(&home, &[".config".into()], &path, compressed, Manifest { version: 1, packages: vec![] }).unwrap();
            let p = preview(&path, &target).unwrap();
            assert_eq!(p.entries.len(), 1);
            restore(&path, &target, &p).unwrap();
            assert_eq!(fs::read_to_string(target.join(".config/app/settings")).unwrap(), "original");
        }
        fs::remove_dir_all(target.join(".config")).unwrap();
        std::os::unix::fs::symlink(&home, target.join(".config")).unwrap();
        assert!(preview(&root.join("archive.gz"), &target).is_err());
        fs::remove_dir_all(root).unwrap();
    }
}

#[cfg(test)]
mod hostile_backups {
    use super::*;
    #[test]
    fn refuses_archive_links_duplicates_and_outside_namespace() {
        let root = std::env::temp_dir().join(format!("athena-hostile-{}", std::process::id()));
        fs::create_dir_all(&root).unwrap();
        for (kind, paths) in [
            (tar::EntryType::Symlink, vec!["home/.config/link"]),
            (tar::EntryType::Regular, vec!["home/file", "home/file"]),
            (tar::EntryType::Regular, vec!["etc/passwd"]),
        ] {
            let path = root.join(format!("case-{}.gz", paths[0].replace('/', "-")));
            let file = File::create(&path).unwrap();
            let mut archive = tar::Builder::new(GzEncoder::new(file, Compression::default()));
            let index = Index {
                version: 1,
                packages: Manifest { version: 1, packages: vec![] },
                skipped: vec![],
                files: vec![],
            };
            let json = serde_json::to_vec(&index).unwrap();
            let mut header = tar::Header::new_gnu();
            header.set_size(json.len() as u64);
            header.set_mode(0o600);
            header.set_cksum();
            archive.append_data(&mut header, "athena-backup.json", &json[..]).unwrap();
            for path in paths {
                let mut header = tar::Header::new_gnu();
                header.set_entry_type(kind);
                header.set_mode(0o600);
                if kind == tar::EntryType::Symlink {
                    header.set_size(0);
                    header.set_link_name("/etc/passwd").unwrap();
                    header.set_cksum();
                    archive.append_data(&mut header, path, &[][..]).unwrap();
                } else {
                    header.set_size(3);
                    header.set_cksum();
                    archive.append_data(&mut header, path, &b"bad"[..]).unwrap();
                }
            }
            archive.into_inner().unwrap().finish().unwrap();
            assert!(preview(&path, &root).is_err());
        }
        assert!(relative(Path::new("../file")).is_err());
        assert!(relative(Path::new("/absolute")).is_err());
        fs::remove_dir_all(root).unwrap();
    }
    #[test]
    fn changed_backup_does_not_replace_existing_destination() {
        let root = std::env::temp_dir().join(format!("athena-changed-{}", std::process::id()));
        let home = root.join("source");
        let target = root.join("destination");
        fs::create_dir_all(&home).unwrap();
        fs::create_dir_all(&target).unwrap();
        fs::write(home.join(".test"), "approved").unwrap();
        fs::write(target.join(".test"), "keep").unwrap();
        let path = root.join("backup");
        create(&home, &[".test".into()], &path, false, Manifest { version: 1, packages: vec![] }).unwrap();
        let approved = preview(&path, &target).unwrap();
        assert!(approved.entries[0].overwrite);
        fs::write(path.join("home/.test"), "changed!").unwrap();
        assert!(restore(&path, &target, &approved).is_err());
        assert_eq!(fs::read_to_string(target.join(".test")).unwrap(), "keep");
        fs::remove_dir_all(root).unwrap();
    }
}
