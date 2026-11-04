//! Athena: a package manager for every Linux source, in GTK and libadwaita.

mod actions;
mod context;
mod package_list;
mod pages;
#[cfg(debug_assertions)]
mod screenshots;
mod terminal;
mod util;
mod window;

use adw::prelude::*;
use gtk::{gio, glib};

pub const APP_ID: &str = "io.github.niruxx.Athena";

const CSS: &str = "
.source-pill {
    padding: 1px 8px;
    border-radius: 999px;
    background: alpha(currentColor, 0.08);
    opacity: 0.85;
}
.badge {
    padding: 0 7px;
    min-width: 10px;
    border-radius: 999px;
    background: var(--accent-bg-color, @accent_bg_color);
    color: var(--accent-fg-color, @accent_fg_color);
    font-size: smaller;
    font-weight: bold;
}
listview.package-list { background: none; }
listview.package-list > row { padding: 0; }
";

fn main() -> glib::ExitCode {
    let app = adw::Application::builder().application_id(APP_ID).flags(gio::ApplicationFlags::default()).build();
    app.connect_startup(|_| {
        let provider = gtk::CssProvider::new();
        provider.load_from_string(CSS);
        if let Some(display) = gtk::gdk::Display::default() {
            gtk::style_context_add_provider_for_display(&display, &provider, gtk::STYLE_PROVIDER_PRIORITY_APPLICATION);
        }
    });
    app.connect_activate(|app| {
        if let Some(window) = app.active_window() {
            window.present();
            return;
        }
        window::build(app).present();
    });
    let quit = gio::SimpleAction::new("quit", None);
    {
        let app = app.clone();
        quit.connect_activate(move |_, _| app.quit());
    }
    app.add_action(&quit);
    app.run()
}
