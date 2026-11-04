//! Installed desktop applications, using GIO's desktop-file parser and launcher.
use gtk::{gio, prelude::*};

pub fn find(package: &str) -> Option<gio::AppInfo> {
    gio::AppInfo::all().into_iter().find(|app| {
        let id = app.id().unwrap_or_default();
        let executable = app.executable();
        id.trim_end_matches(".desktop") == package
            || executable.file_name().is_some_and(|name| name == package)
    })
}
