//! pkexec can only ask for a password through a polkit authentication agent
//! running in the user's session. Full desktops start their own (KDE,
//! GNOME, Cinnamon, …); bare compositors (niri, Hyprland, sway) and minimal
//! installs often don't, and then every privileged action fails without a
//! prompt. This finds a running agent and, when there's none, starts an
//! installed one for the rest of the session.

use std::os::unix::fs::{MetadataExt, PermissionsExt};
use std::path::Path;
use std::thread;
use std::time::Duration;

/// Programs that register a polkit agent: standalone agents, and desktop
/// shells with one built in.
const AGENT_NAMES: &[&str] = &[
    "polkit-kde-authentication-agent-1",
    "polkit-gnome-authentication-agent-1",
    "polkit-mate-authentication-agent-1",
    "polkit-efl-authentication-agent-1",
    "lxqt-policykit-agent",
    "lxpolkit",
    "xfce-polkit",
    "hyprpolkitagent",
    "soteria",
    "mate-polkit",
    "ukui-polkit",
    "io.elementary.desktop.agent-polkit",
    "budgie-polkit-dialog",
    "gnome-shell",
    "cinnamon",
    "cosmic-osd",
    "phosh",
];

/// Where distributions install the agents Athena can start, in order of
/// preference (GTK ones first, to match the app).
const AGENT_PATHS: &[&str] = &[
    "/usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1", // Arch, Debian
    "/usr/libexec/polkit-gnome-authentication-agent-1",          // Fedora
    "/usr/bin/soteria",
    "/usr/lib/mate-polkit/polkit-mate-authentication-agent-1",
    "/usr/libexec/polkit-mate-authentication-agent-1",
    "/usr/lib/xfce-polkit/xfce-polkit",
    "/usr/lib/polkit-kde-authentication-agent-1",                          // Arch
    "/usr/libexec/polkit-kde-authentication-agent-1",                      // Fedora, openSUSE
    "/usr/lib/x86_64-linux-gnu/libexec/polkit-kde-authentication-agent-1", // Debian, Ubuntu
    "/usr/lib/aarch64-linux-gnu/libexec/polkit-kde-authentication-agent-1",
    "/usr/bin/lxqt-policykit-agent",
    "/usr/lib/hyprpolkitagent/hyprpolkitagent",
    "/usr/libexec/hyprpolkitagent",
    "/usr/bin/lxpolkit",
];

fn program_name(pid_dir: &Path) -> Option<String> {
    let cmdline = std::fs::read(pid_dir.join("cmdline")).ok()?;
    let first = cmdline.split(|b| *b == 0).next()?;
    let path = String::from_utf8_lossy(first);
    Some(path.rsplit('/').next().unwrap_or(&path).to_owned())
}

/// Whether a known agent (or a desktop shell with one built in) is running
/// as this user.
pub fn is_running() -> bool {
    // /proc/self is owned by the user this process runs as.
    let Ok(uid) = std::fs::metadata("/proc/self").map(|m| m.uid()) else {
        return false;
    };
    let Ok(entries) = std::fs::read_dir("/proc") else { return false };
    entries.flatten().any(|entry| {
        let path = entry.path();
        let is_pid = entry.file_name().to_string_lossy().bytes().all(|b| b.is_ascii_digit());
        is_pid
            && std::fs::metadata(&path).map(|m| m.uid() == uid).unwrap_or(false)
            && program_name(&path).is_some_and(|name| AGENT_NAMES.contains(&name.as_str()))
    })
}

/// The first agent program installed on this system.
pub fn installed_agent() -> Option<&'static str> {
    AGENT_PATHS.iter().copied().find(|path| {
        std::fs::metadata(path)
            .map(|m| m.is_file() && m.permissions().mode() & 0o111 != 0)
            .unwrap_or(false)
    })
}

/// Starts [`installed_agent`] if no agent is running. Returns whether an
/// agent is running afterwards. Cheap enough to call before every
/// privileged command.
pub fn ensure_running() -> bool {
    if is_running() {
        return true;
    }
    let Some(agent) = installed_agent() else { return false };
    let Ok(mut child) = std::process::Command::new(agent)
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()
    else {
        return false;
    };
    // It keeps running for the session; reap it whenever it exits.
    thread::spawn(move || {
        let _ = child.wait();
    });
    // Give it a moment to register with polkit before pkexec asks for it.
    for _ in 0..10 {
        if is_running() {
            break;
        }
        thread::sleep(Duration::from_millis(100));
    }
    thread::sleep(Duration::from_millis(300));
    true
}
