//! Running backend work off the main thread, and small widget helpers.

use gtk::glib;
use gtk::prelude::*;

/// Runs `work` on a worker thread, then `done` with its result back on the
/// main thread. Backend calls block on subprocesses, so they all go
/// through here.
pub fn background<T, W, D>(work: W, done: D)
where
    T: Send + 'static,
    W: FnOnce() -> T + Send + 'static,
    D: FnOnce(T) + 'static,
{
    glib::spawn_future_local(async move {
        if let Ok(result) = gtk::gio::spawn_blocking(work).await {
            done(result);
        }
    });
}

/// A label with a style class, left-aligned, ellipsized.
pub fn label(text: &str, classes: &[&str]) -> gtk::Label {
    let label = gtk::Label::builder().label(text).xalign(0.0).ellipsize(gtk::pango::EllipsizeMode::End).build();
    for class in classes {
        label.add_css_class(class);
    }
    label
}

/// A spinner page for "loading…".
pub fn loading_page(title: &str) -> adw::StatusPage {
    let spinner = gtk::Spinner::builder().spinning(true).width_request(32).height_request(32).halign(gtk::Align::Center).build();
    adw::StatusPage::builder().title(title).child(&spinner).build()
}

/// The icon for a package: the app's own icon when the icon theme has one
/// under its name (Flatpak app IDs, most desktop apps), else a generic one.
pub fn package_icon(name: &str) -> String {
    let display = gtk::gdk::Display::default();
    let has = display.is_some_and(|d| gtk::IconTheme::for_display(&d).has_icon(name));
    if has { name.to_owned() } else { "package-x-generic-symbolic".to_owned() }
}
