//! Athena: a package manager for every Linux source, in GTK and libadwaita.

mod actions;
mod aur_review;
mod background;
mod catalog_ui;
mod context;
mod desktop;
mod details;
mod i18n;
mod package_list;
mod pages;
mod permissions;
#[cfg(debug_assertions)]
mod screenshots;
mod settings;
mod setup;
mod terminal;
mod tray;
mod util;
mod versions;
mod window;

use adw::prelude::*;
use gtk::{gio, glib};

pub const APP_ID: &str = "io.github.niruxx.Athena";

const CSS: &str = "
/* ---------- motion ---------- */
@keyframes athena-fade-in { from { opacity: 0; } to { opacity: 1; } }
@keyframes athena-rise { from { opacity: 0; margin-top: 14px; margin-bottom: -14px; } to { opacity: 1; margin-top: 0; margin-bottom: 0; } }
@keyframes athena-pop { from { opacity: 0; transform: scale(0.94); } to { opacity: 1; transform: scale(1); } }
@keyframes athena-pulse { 0% { opacity: 1; } 50% { opacity: 0.45; } 100% { opacity: 1; } }
.page-enter { animation: athena-fade-in 260ms ease-out; }
.rise-in { animation: athena-rise 360ms cubic-bezier(0.2, 0.9, 0.3, 1); }
.pop-in { animation: athena-pop 420ms cubic-bezier(0.2, 0.9, 0.3, 1.2); }
button, row, .card, .app-tile, .filter-chip, .news-card {
    transition: background 180ms ease, box-shadow 220ms ease, color 180ms ease, opacity 200ms ease, transform 200ms ease;
}
.navigation-sidebar > row { margin: 2px 6px; border-radius: 10px; transition: background 180ms ease; }
.navigation-sidebar > row:selected image { color: var(--accent-color); }
.sidebar-heading { margin: 14px 14px 4px 14px; font-size: 0.78em; font-weight: 800; letter-spacing: 0.06em; opacity: 0.55; }

/* ---------- small pieces ---------- */
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
.filter-chip { border-radius: 999px; padding: 6px 14px; }
.filter-chip:checked { background: var(--accent-bg-color); color: var(--accent-fg-color); }
.filter-bar { padding: 4px 0; }
.filter-bar > dropdown > button, .filter-bar > menubutton > button { border-radius: 999px; padding: 6px 12px; }

/* Roomy action rows: buttons never touch the next row. */
.spaced-list > row { min-height: 64px; }
.spaced-list > row button { margin: 10px 0; }
preferencesgroup .boxed-list > row button.row-action { margin-top: 10px; margin-bottom: 10px; }
.action-buttons { margin-top: 6px; }
.action-buttons > button { padding: 8px 16px; }

/* ---------- discover ---------- */
.discover-hero { padding: 16px 18px 12px 18px; border-radius: 20px;
    background: linear-gradient(135deg, alpha(var(--accent-bg-color), 0.22), alpha(var(--accent-bg-color), 0.04)); }
.discover-hero .hero-title { font-size: 1.7em; font-weight: 800; }
.category-strip { padding: 2px 2px 8px 2px; }
.discover-grid { background: transparent; }
.discover-grid > child { padding: 9px; background: none; }
.discover-grid > child:focus .app-tile { outline: 2px solid var(--accent-color); outline-offset: 2px; }
.app-tile { padding: 20px 22px; border-radius: 18px; }
.app-tile:hover { box-shadow: 0 6px 18px alpha(black, 0.12), 0 0 0 1px alpha(var(--accent-color), 0.35); background: alpha(var(--accent-bg-color), 0.06); }
.app-tile .app-icon { -gtk-icon-shadow: 0 2px 6px alpha(black, 0.18); }
.app-tile .app-title { font-size: 1.1em; font-weight: 700; }
.app-tile .app-summary { line-height: 1.35; }
.app-tile .app-footer { padding-top: 12px; border-top: 1px solid alpha(currentColor, 0.08); }
.app-indicator { padding: 3px 9px; border-radius: 999px; background: alpha(currentColor, 0.07); font-size: 0.82em; }
.app-indicator.installed { color: var(--success-color); background: alpha(var(--success-color), 0.12); }
.app-indicator.accent { color: var(--accent-color); background: alpha(var(--accent-color), 0.12); }

/* ---------- search ---------- */
.discover-hero entry, .search-hero entry { min-height: 40px; border-radius: 12px; font-size: 1.05em; }
.search-intro { padding: 24px; border-radius: 18px; }
.suggestion-chip { border-radius: 999px; padding: 6px 14px; }

/* ---------- news ---------- */
.news-card { padding: 18px 20px; border-radius: 16px; }
.news-card:hover { box-shadow: 0 4px 14px alpha(black, 0.10); background: alpha(var(--accent-bg-color), 0.05); }
.news-card .news-title { font-size: 1.08em; font-weight: 700; }
.news-card.unread .news-title { color: var(--accent-color); }
.unread-dot { min-width: 9px; min-height: 9px; border-radius: 999px; background: var(--accent-bg-color); animation: athena-pulse 2.4s ease-in-out infinite; }
.news-article { padding: 34px 38px; border-radius: 22px; }
.news-article .article-title { font-size: 1.8em; font-weight: 800; }
.news-article .article-body { font-size: 1.06em; line-height: 1.55; }

/* ---------- source pages / permissions ---------- */
.identity-card { padding: 22px 24px; border-radius: 20px;
    background: linear-gradient(135deg, alpha(var(--accent-bg-color), 0.16), alpha(var(--accent-bg-color), 0.03)); }
.identity-card .identity-title { font-size: 1.45em; font-weight: 800; }
.permission-icon { padding: 8px; border-radius: 10px; background: alpha(var(--accent-bg-color), 0.14); color: var(--accent-color); }
.permission-icon.warning { background: alpha(var(--warning-color), 0.15); color: var(--warning-color); }
.permission-identity { padding: 20px; }
.stat-pill { padding: 4px 11px; border-radius: 999px; background: alpha(currentColor, 0.08); font-size: 0.86em; }

/* ---------- setup ---------- */
.setup-logo { -gtk-icon-shadow: 0 8px 24px alpha(black, 0.22); }
.setup-title { font-size: 2.0em; font-weight: 800; }
.feature-tile { padding: 14px 16px; border-radius: 14px; }
.feature-tile image { color: var(--accent-color); }
carouselindicatordots { margin: 10px; }

listview.package-list { background: none; }
listview.package-list > row { padding: 0; }
";

fn main() -> glib::ExitCode {
    let args = std::env::args_os().collect::<Vec<_>>();
    if args.get(1).is_some_and(|arg| arg == "--execute-reviewed-aur") {
        let Some(path) = args.get(2) else {
            eprintln!("Missing reviewed transaction");
            return glib::ExitCode::FAILURE;
        };
        return match athena_core::aur::execute_manifest(std::path::Path::new(path)) {
            Ok(()) => glib::ExitCode::SUCCESS,
            Err(error) => {
                eprintln!("AUR build stopped: {error}");
                glib::ExitCode::FAILURE
            }
        };
    }
    i18n::init();
    let app = adw::Application::builder()
        .application_id(APP_ID)
        .flags(gio::ApplicationFlags::HANDLES_COMMAND_LINE)
        .build();
    app.connect_startup(|_| {
        let provider = gtk::CssProvider::new();
        provider.load_from_string(CSS);
        if let Some(display) = gtk::gdk::Display::default() {
            gtk::style_context_add_provider_for_display(&display, &provider, gtk::STYLE_PROVIDER_PRIORITY_APPLICATION);
        }
    });
    // Loaded once and applied before the first window, so it never flashes
    // the wrong style; `settings::apply` keeps it in step afterwards.
    let settings = settings::load();
    {
        let settings = settings.clone();
        app.connect_startup(move |_| {
            if let Some(settings) = &settings {
                settings::apply(settings);
            }
        });
    }
    app.add_main_option(
        "background",
        glib::Char::from(0),
        glib::OptionFlags::NONE,
        glib::OptionArg::None,
        "Start background update checks without opening a window",
        None,
    );
    app.add_main_option(
        "updates",
        glib::Char::from(0),
        glib::OptionFlags::NONE,
        glib::OptionArg::None,
        "Open the Updates page",
        None,
    );
    app.connect_startup(background::install);
    app.connect_command_line(|app, command| {
        let background = command.options_dict().contains("background");
        if command.options_dict().contains("updates") {
            app.activate_action("show-updates", None);
        } else if !background {
            app.activate();
        } else if !settings::boolean("background-updates") {
            app.quit();
        }
        glib::ExitCode::SUCCESS
    });
    app.connect_activate(|app| {
        if let Some(window) = app.active_window().or_else(|| app.windows().first().cloned()) {
            window.present();
            return;
        }
        window::build(app).present();
    });
    for name in ["show-updates", "update"] {
        let action = gio::SimpleAction::new(name, None);
        let weak = app.downgrade();
        action.connect_activate(move |_, _| {
            if let Some(app) = weak.upgrade() {
                app.activate();
                if let Some(window) = app.active_window() {
                    let _ = gtk::prelude::WidgetExt::activate_action(&window, if name == "update" { "win.update-all" } else { "win.show-updates" }, None);
                }
            }
        });
        app.add_action(&action);
    }
    let quit = gio::SimpleAction::new("quit", None);
    {
        let app = app.clone();
        quit.connect_activate(move |_, _| app.quit());
    }
    app.add_action(&quit);
    let preferences = gio::SimpleAction::new("preferences", None);
    preferences.set_enabled(settings.is_some());
    {
        let app = app.clone();
        preferences.connect_activate(move |_, _| {
            if let Some(settings) = &settings {
                let dialog = settings::dialog(settings);
                if let Some(window) = app.active_window() {
                    settings::add_tools(&dialog, &window);
                    dialog.present(Some(&window));
                }
            }
        });
    }
    app.add_action(&preferences);
    app.set_accels_for_action("app.preferences", &["<Control>comma"]);
    app.run()
}
