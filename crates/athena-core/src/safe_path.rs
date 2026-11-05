//! Open each directory component without following symlinks. Keeping directory
//! descriptors alive anchors operations even when another process renames paths.
use rustix::fs::{self, Mode, OFlags};
use std::{
    os::fd::OwnedFd,
    path::{Component, Path},
};
pub fn parent(root: &Path, path: &Path, create: bool) -> Result<OwnedFd, String> {
    if path.as_os_str().is_empty() || !path.components().all(|c| matches!(c, Component::Normal(_))) {
        return Err("Unsafe relative path".into());
    }
    let mut fd = fs::open(root, OFlags::RDONLY | OFlags::DIRECTORY | OFlags::NOFOLLOW | OFlags::CLOEXEC, Mode::empty()).map_err(|e| e.to_string())?;
    for component in path.parent().unwrap_or(Path::new("")).components() {
        let name = component.as_os_str();
        if create {
            match fs::mkdirat(&fd, name, Mode::RWXU) {
                Ok(()) | Err(rustix::io::Errno::EXIST) => {}
                Err(e) => return Err(e.to_string()),
            }
        }
        fd = fs::openat(
            &fd,
            name,
            OFlags::RDONLY | OFlags::DIRECTORY | OFlags::NOFOLLOW | OFlags::CLOEXEC,
            Mode::empty(),
        )
        .map_err(|e| e.to_string())?;
    }
    Ok(fd)
}
pub fn read(root: &Path, path: &Path) -> Result<std::fs::File, String> {
    let parent = parent(root, path, false)?;
    let fd = fs::openat(
        &parent,
        path.file_name().ok_or("Missing file name")?,
        OFlags::RDONLY | OFlags::NOFOLLOW | OFlags::NONBLOCK | OFlags::CLOEXEC,
        Mode::empty(),
    )
    .map_err(|e| e.to_string())?;
    let file = std::fs::File::from(fd);
    if !file.metadata().map_err(|e| e.to_string())?.is_file() {
        return Err("Not a regular file".into());
    }
    Ok(file)
}
