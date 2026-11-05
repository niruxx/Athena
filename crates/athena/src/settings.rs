//! Preferences, stored in GSettings (schema `io.github.niruxx.Athena`).

use adw::prelude::*;
use gtk::gio;

const COLOR_SCHEMES: [(&str, &str); 3] = [("default", "Follow System"), ("light", "Light"), ("dark", "Dark")];

/// The app's settings: the installed schema, or in debug builds the copy
/// build.rs compiled. `None` when neither exists (an uninstalled release
/// build), since `gio::Settings::new` aborts on a missing schema.
pub fn load() -> Option<gio::Settings> {
    let installed = gio::SettingsSchemaSource::default().and_then(|source| source.lookup(crate::APP_ID, true));
    #[cfg(debug_assertions)]
    let installed = {
        gio::SettingsSchemaSource::from_directory(env!("ATHENA_SCHEMA_DIR"), gio::SettingsSchemaSource::default().as_ref(), false)
            .ok()
            .and_then(|source| source.lookup(crate::APP_ID, false))
            .or(installed)
    };
    let schema = installed?;
    if !schema.has_key("flatpak-enabled") {
        return None;
    }
    Some(gio::Settings::new_full(&schema, None::<&gio::SettingsBackend>, None))
}

fn apply_color_scheme(settings: &gio::Settings) {
    let scheme = match settings.string("color-scheme").as_str() {
        "light" => adw::ColorScheme::ForceLight,
        "dark" => adw::ColorScheme::ForceDark,
        _ => adw::ColorScheme::Default,
    };
    adw::StyleManager::default().set_color_scheme(scheme);
}

/// Applies the stored appearance now and whenever it changes.
pub fn apply(settings: &gio::Settings) {
    apply_color_scheme(settings);
    settings.connect_changed(Some("color-scheme"), |settings, _| apply_color_scheme(settings));
}

/// The Preferences dialog.
pub fn dialog(settings: &gio::Settings) -> adw::PreferencesDialog {
    let labels: Vec<&str> = COLOR_SCHEMES.iter().map(|(_, label)| *label).collect();
    let appearance = adw::ComboRow::builder()
        .title(crate::i18n::gettext("Appearance"))
        .model(&gtk::StringList::new(&labels))
        .build();
    let current = settings.string("color-scheme");
    let index = COLOR_SCHEMES.iter().position(|(key, _)| *key == current.as_str()).unwrap_or(0);
    appearance.set_selected(index as u32);
    {
        let settings = settings.clone();
        appearance.connect_selected_notify(move |row| {
            if let Some((key, _)) = COLOR_SCHEMES.get(row.selected() as usize) {
                let _ = settings.set_string("color-scheme", key);
            }
        });
    }

    let group = adw::PreferencesGroup::builder().title(crate::i18n::gettext("Appearance")).build();
    group.add(&appearance);
    let page = adw::PreferencesPage::builder()
        .title(crate::i18n::gettext("General"))
        .icon_name("preferences-system-symbolic")
        .build();
    page.add(&group);
    let general = adw::PreferencesGroup::builder().title(crate::i18n::gettext("Everyday use")).build();
    combo(
        settings,
        &general,
        "Start on page",
        "start-page",
        &[
            "search",
            "installed",
            "updates",
            "repositories",
            "history",
            "discover",
            "flatpak",
            "snap",
            "arch",
            "home",
            "news",
            "tools",
        ],
    );
    for (title, key) in [
        (crate::i18n::gettext_noop("Close terminal automatically on success"), "close-on-success"),
        (crate::i18n::gettext_noop("Run transactions without asking"), "skip-confirmations"),
        (crate::i18n::gettext_noop("Show architecture"), "show-architecture"),
        (crate::i18n::gettext_noop("Show size"), "show-size"),
        (crate::i18n::gettext_noop("Compact rows"), "compact-rows"),
        (crate::i18n::gettext_noop("Check for Athena updates on startup"), "check-releases"),
        (
            crate::i18n::gettext_noop("Take one system snapshot before each transaction batch"),
            "snapshot-before-transactions",
        ),
    ] {
        let row = adw::SwitchRow::builder().title(crate::i18n::gettext(title)).build();
        settings.bind(key, &row, "active").build();
        general.add(&row);
    }
    let expiry = adw::SpinRow::with_range(0.0, 8760.0, 1.0);
    expiry.set_title(&crate::i18n::gettext("DNF metadata expiry (hours, after restart)"));
    expiry.set_value(settings.uint("dnf-metadata-hours") as f64);
    let stored = settings.clone();
    expiry.connect_value_notify(move |row| {
        let _ = stored.set_uint("dnf-metadata-hours", row.value() as u32);
    });
    general.add(&expiry);
    let folder = adw::EntryRow::builder()
        .title(crate::i18n::gettext("Log folder (empty disables logging)"))
        .build();
    settings.bind("log-folder", &folder, "text").build();
    general.add(&folder);
    combo(settings, &general, "Logging level", "log-level", &["error", "info", "debug"]);
    let background = adw::SwitchRow::builder()
        .title(crate::i18n::gettext("Check package updates in the background"))
        .subtitle(crate::i18n::gettext(
            "Requests desktop permission and login startup. Closing Athena keeps checks running; Quit stops them.",
        ))
        .build();
    settings.bind("background-updates", &background, "active").build();
    general.add(&background);
    let interval = adw::SpinRow::with_range(15., 1440., 15.);
    interval.set_title(&crate::i18n::gettext("Update check interval (minutes)"));
    interval.set_value(settings.uint("update-check-minutes") as f64);
    let stored = settings.clone();
    interval.connect_value_notify(move |row| {
        let _ = stored.set_uint("update-check-minutes", row.value() as u32);
    });
    general.add(&interval);
    page.add(&general);
    let dialog = adw::PreferencesDialog::new();
    dialog.add(&page);
    dialog
}

fn combo(settings: &gio::Settings, group: &adw::PreferencesGroup, title: &str, key: &'static str, values: &'static [&'static str]) {
    let row = adw::ComboRow::builder()
        .title(crate::i18n::gettext(title))
        .model(&gtk::StringList::new(values))
        .build();
    row.set_selected(values.iter().position(|v| *v == settings.string(key)).unwrap_or(0) as u32);
    let settings = settings.clone();
    row.connect_selected_notify(move |row| {
        if let Some(value) = values.get(row.selected() as usize) {
            let _ = settings.set_string(key, value);
        }
    });
    group.add(&row);
}

pub fn boolean(key: &str) -> bool {
    load().is_some_and(|s| s.boolean(key))
}

/// Append transaction diagnostics to a user-selected folder. Logging failure
/// must never prevent a package transaction from completing.
pub fn log(text: &str, error: bool) {
    use std::io::Write;
    let Some(settings) = load() else { return };
    let folder = settings.string("log-folder");
    if folder.is_empty() || (!error && settings.string("log-level") == "error") {
        return;
    }
    if std::fs::create_dir_all(folder.as_str()).is_err() {
        return;
    }
    if let Ok(mut file) = std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(std::path::Path::new(folder.as_str()).join("athena.log"))
    {
        let text = if settings.string("log-level") == "debug" || error {
            text.to_owned()
        } else {
            "Transaction completed successfully".to_owned()
        };
        let _ = writeln!(file, "{:?} {}", std::time::SystemTime::now(), text);
    }
}

/// Source management uses the same visible, authenticated transaction runner.
pub fn add_tools(dialog: &adw::PreferencesDialog, parent: &gtk::Window) {
    let page = adw::PreferencesPage::builder()
        .title(crate::i18n::gettext("Sources and Setup"))
        .icon_name("network-server-symbolic")
        .build();
    let group = adw::PreferencesGroup::builder()
        .title(crate::i18n::gettext("Package sources"))
        .description(crate::i18n::gettext("Restart Athena after changing source support."))
        .build();
    for (name, installed, install, remove) in [
        (
            "Flatpak",
            athena_core::distro::flatpak_installed(),
            athena_core::distro::flatpak_install_commands(),
            athena_core::distro::flatpak_remove_commands(),
        ),
        (
            "Snap",
            athena_core::distro::snap_installed(),
            athena_core::distro::snap_install_commands(),
            athena_core::distro::snap_remove_commands(),
        ),
    ] {
        let row = adw::ActionRow::builder()
            .title(name)
            .subtitle(if installed { "Installed" } else { "Not installed" })
            .build();
        let button = gtk::Button::builder()
            .label(if installed { "Remove…" } else { "Install…" })
            .valign(gtk::Align::Center)
            .build();
        let commands = if installed { remove } else { install };
        button.set_sensitive(!commands.is_empty());
        let parent = parent.clone();
        button.connect_clicked(move |_| {
            let confirm = adw::AlertDialog::new(
                Some(&format!("{} {name} support?", if installed { "Remove" } else { "Install" })),
                Some("The system package manager will change source support. Review the terminal output before restarting Athena."),
            );
            confirm.add_responses(&[("cancel", &crate::i18n::gettext("Cancel")), ("apply", &crate::i18n::gettext("Apply"))]);
            confirm.set_close_response("cancel");
            let (parent2, commands) = (parent.clone(), commands.clone());
            confirm.connect_response(Some("apply"), move |_, _| {
                crate::terminal::run(&parent2, "Configure source support", commands.clone(), boolean("close-on-success"), |_| {})
            });
            confirm.present(Some(&parent));
        });
        row.add_suffix(&button);
        group.add(&row);
    }
    if let Some(settings)=load(){
        for (title,key,installed) in [("Show Flatpak management (after restart)","flatpak-enabled",athena_core::distro::flatpak_installed()),("Show Snap management (after restart)","snap-enabled",athena_core::distro::snap_installed())]{
            let row=adw::SwitchRow::builder().title(title).subtitle(if installed{"Show this source in navigation and search."}else{"Install source support first."}).sensitive(installed).build();settings.bind(key,&row,"active").build();group.add(&row);
        }
    }
    if athena_core::distro::detect_family() == athena_core::distro::DistroFamily::Arch
        && let Some(settings) = load()
    {
        let row = adw::SwitchRow::builder()
            .title(crate::i18n::gettext("AUR source (after restart)"))
            .subtitle(crate::i18n::gettext("Every build requires PKGBUILD review."))
            .build();
        settings.bind("aur-enabled", &row, "active").build();
        group.add(&row);
    }
    let setup = gtk::Button::with_label(&crate::i18n::gettext("Run First-time Setup Again"));
    setup.set_action_name(Some("win.setup"));
    group.add(&setup);
    page.add(&group);
    dialog.add(&page);
}

pub fn source_enabled(key:&str)->bool {
    let setting=match key{"flatpak"=>"flatpak-enabled","snap"=>"snap-enabled",_=>return true};
    load().is_none_or(|s|s.boolean(setting))
}
