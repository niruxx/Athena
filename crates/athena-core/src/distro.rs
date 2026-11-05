//! Which distribution this is, and which backends it gets.

use std::sync::Arc;

use crate::backend::Backend;
use crate::backends::{AptBackend, DnfBackend, FlatpakBackend, PacmanBackend, SnapBackend};
use crate::process::{Command, executable_exists};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DistroFamily {
    Fedora,
    Debian,
    Arch,
    Unknown,
}

/// From /etc/os-release's ID and ID_LIKE.
pub fn detect_family() -> DistroFamily {
    family_from_os_release(&std::fs::read_to_string("/etc/os-release").unwrap_or_default())
}

pub fn family_from_os_release(text: &str) -> DistroFamily {
    let ids: String = text
        .lines()
        .filter(|l| l.starts_with("ID=") || l.starts_with("ID_LIKE="))
        .map(|l| l.split_once('=').map(|(_, v)| v.trim_matches('"').to_lowercase()).unwrap_or_default() + " ")
        .collect();
    if ["fedora", "rhel", "centos"].iter().any(|id| ids.contains(id)) {
        DistroFamily::Fedora
    } else if ids.contains("debian") || ids.contains("ubuntu") {
        DistroFamily::Debian
    } else if ids.contains("arch") {
        DistroFamily::Arch
    } else {
        DistroFamily::Unknown
    }
}

/// The system's own package manager: the one matching /etc/os-release, or
/// else whichever supported one is on PATH.
pub fn native_backend() -> Option<Arc<dyn Backend>> {
    let candidates: [(DistroFamily, Arc<dyn Backend>); 3] = [
        (DistroFamily::Fedora, Arc::new(DnfBackend::new())),
        (DistroFamily::Debian, Arc::new(AptBackend)),
        (DistroFamily::Arch, Arc::new(PacmanBackend)),
    ];
    let family = detect_family();
    if let Some((_, backend)) = candidates.iter().find(|(f, b)| *f == family && b.is_available()) {
        return Some(backend.clone());
    }
    candidates.into_iter().map(|(_, b)| b).find(|b| b.is_available())
}

/// Every source on this system: the native package manager first, then
/// Flatpak and Snap when they're installed.
pub fn all_backends() -> Vec<Arc<dyn Backend>> {
    let mut backends: Vec<Arc<dyn Backend>> = native_backend().into_iter().collect();
    if FlatpakBackend.is_available() {
        backends.push(Arc::new(FlatpakBackend));
    }
    if SnapBackend.is_available() {
        backends.push(Arc::new(SnapBackend));
    }
    if detect_family() == DistroFamily::Arch && executable_exists("pacman") {
        backends.push(Arc::new(crate::aur::AurBackend));
    }
    backends
}

pub fn flatpak_installed() -> bool {
    executable_exists("flatpak")
}

pub fn snap_installed() -> bool {
    executable_exists("snap")
}

/// Installs Flatpak support with the system package manager.
pub fn flatpak_install_commands() -> Vec<Command> {
    match detect_family() {
        DistroFamily::Fedora => vec![Command::privileged(["dnf", "install", "-y", "flatpak"])],
        DistroFamily::Debian => vec![Command::privileged(["apt-get", "install", "-y", "flatpak"])],
        DistroFamily::Arch => vec![Command::privileged(["pacman", "-S", "--noconfirm", "flatpak"])],
        DistroFamily::Unknown => Vec::new(),
    }
}

pub fn flatpak_remove_commands() -> Vec<Command> {
    match detect_family() {
        DistroFamily::Fedora => vec![Command::privileged(["dnf", "remove", "-y", "flatpak"])],
        DistroFamily::Debian => vec![Command::privileged(["apt-get", "remove", "-y", "flatpak"])],
        DistroFamily::Arch => vec![Command::privileged(["pacman", "-R", "--noconfirm", "flatpak"])],
        DistroFamily::Unknown => Vec::new(),
    }
}

/// Installs Snap support. Arch has no official snapd package (it's in the
/// AUR), so there's nothing to run there.
pub fn snap_install_commands() -> Vec<Command> {
    match detect_family() {
        // Fedora's snapd doesn't create the /snap link classic snaps need.
        DistroFamily::Fedora => vec![
            Command::privileged(["dnf", "install", "-y", "snapd"]),
            Command::privileged(["ln", "-sf", "/var/lib/snapd/snap", "/snap"]),
        ],
        DistroFamily::Debian => vec![Command::privileged(["apt-get", "install", "-y", "snapd"])],
        DistroFamily::Arch | DistroFamily::Unknown => Vec::new(),
    }
}

pub fn snap_remove_commands() -> Vec<Command> {
    match detect_family() {
        DistroFamily::Fedora => vec![
            Command::privileged(["dnf", "remove", "-y", "snapd"]),
            Command::privileged(["rm", "-f", "/snap"]),
        ],
        DistroFamily::Debian => vec![Command::privileged(["apt-get", "remove", "-y", "snapd"])],
        DistroFamily::Arch => vec![Command::privileged(["pacman", "-R", "--noconfirm", "snapd"])],
        DistroFamily::Unknown => Vec::new(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn families() {
        assert_eq!(family_from_os_release("ID=fedora\n"), DistroFamily::Fedora);
        assert_eq!(family_from_os_release("ID=linuxmint\nID_LIKE=\"ubuntu debian\"\n"), DistroFamily::Debian);
        assert_eq!(family_from_os_release("ID=endeavouros\nID_LIKE=arch\n"), DistroFamily::Arch);
        assert_eq!(family_from_os_release("ID=void\n"), DistroFamily::Unknown);
    }
}
