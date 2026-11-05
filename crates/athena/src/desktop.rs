//! Installed desktop applications, using GIO's desktop-file parser and launcher.
use gtk::{gio, prelude::*};
use std::{cell::RefCell, collections::HashMap};

thread_local! {
    static APPLICATIONS: RefCell<Option<HashMap<String, gio::AppInfo>>> = const { RefCell::new(None) };
}

fn index() -> HashMap<String, gio::AppInfo> {
    let mut result = HashMap::new();
    for app in gio::AppInfo::all() {
        if !app.should_show() {
            continue;
        }
        if let Some(id) = app.id() {
            result.entry(id.trim_end_matches(".desktop").to_owned()).or_insert_with(|| app.clone());
        }
        if let Some(name) = app.executable().file_name().and_then(|name| name.to_str())
            && !["flatpak", "snap", "env", "sh", "bash", "xdg-open", "gtk-launch"].contains(&name)
        {
            result.entry(name.to_owned()).or_insert_with(|| app.clone());
        }
    }
    result
}

pub fn refresh() {
    APPLICATIONS.with(|apps| *apps.borrow_mut() = None);
}

pub fn find(package: &str) -> Option<gio::AppInfo> {
    APPLICATIONS.with(|apps| {
        let mut apps = apps.borrow_mut();
        apps.get_or_insert_with(index).get(package).cloned()
    })
}
