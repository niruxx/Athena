//! First-run setup: a swipeable, animated walkthrough of sources and
//! preferences, ending with a summary of what Finish will change.
use crate::{context::Context, settings, util};
use adw::prelude::*;
use std::rc::Rc;

fn pretty_name() -> String {
    std::fs::read_to_string("/etc/os-release")
        .unwrap_or_default()
        .lines()
        .find_map(|l| l.strip_prefix("PRETTY_NAME="))
        .map(|v| v.trim_matches('"').to_owned())
        .unwrap_or_else(|| "Linux".into())
}

fn feature(icon: &str, title: &str, body: &str) -> gtk::Box {
    let tile = gtk::Box::builder().spacing(14).build();
    tile.add_css_class("card");
    tile.add_css_class("feature-tile");
    tile.append(&gtk::Image::builder().icon_name(icon).pixel_size(28).valign(gtk::Align::Center).build());
    let text = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(2).hexpand(true).build();
    text.append(&util::label(title, &["heading"]));
    let body = util::label(body, &["dim-label", "caption"]);
    body.set_wrap(true);
    body.set_ellipsize(gtk::pango::EllipsizeMode::None);
    text.append(&body);
    tile.append(&text);
    tile
}

fn scrolled(child: &impl IsA<gtk::Widget>) -> gtk::ScrolledWindow {
    gtk::ScrolledWindow::builder()
        .child(&adw::Clamp::builder().maximum_size(520).child(child).build())
        .hscrollbar_policy(gtk::PolicyType::Never)
        .vexpand(true)
        .hexpand(true)
        .build()
}

fn heading(icon: &str, title: &str, body: &str) -> gtk::Box {
    let column = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(8).margin_bottom(6).build();
    let image = gtk::Image::builder().icon_name(icon).pixel_size(48).halign(gtk::Align::Center).build();
    image.add_css_class("permission-icon");
    column.append(&image);
    let title = gtk::Label::builder().label(title).halign(gtk::Align::Center).build();
    title.add_css_class("title-2");
    column.append(&title);
    let body = gtk::Label::builder().label(body).wrap(true).justify(gtk::Justification::Center).halign(gtk::Align::Center).build();
    body.add_css_class("dim-label");
    column.append(&body);
    column
}

fn source_row(title: &str, subtitle: &str, icon: &str, active: bool, available: bool) -> adw::SwitchRow {
    let row = adw::SwitchRow::builder().title(title).subtitle(subtitle).active(active).build();
    let image = gtk::Image::builder().icon_name(icon).valign(gtk::Align::Center).build();
    image.add_css_class("permission-icon");
    row.add_prefix(&image);
    let state = util::label(if active { "Installed" } else if available { "Not installed" } else { "Unavailable" }, &["app-indicator"]);
    if active {
        state.add_css_class("installed");
    }
    state.set_valign(gtk::Align::Center);
    row.add_suffix(&state);
    row.set_sensitive(available || active);
    row
}

pub fn show(ctx: &Rc<Context>) {
    let dialog = adw::Dialog::builder()
        .title(crate::i18n::gettext("Welcome to Athena"))
        .content_width(620)
        .content_height(640)
        .build();
    let carousel = adw::Carousel::builder().allow_scroll_wheel(false).spacing(24).vexpand(true).build();

    // 1. Welcome.
    let welcome = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(14).margin_top(18).margin_bottom(18).margin_start(24).margin_end(24).build();
    let logo = util::logo_image(112);
    logo.set_halign(gtk::Align::Center);
    logo.add_css_class("pop-in");
    welcome.append(&logo);
    let title = gtk::Label::builder().label(crate::i18n::gettext("Welcome to Athena")).halign(gtk::Align::Center).build();
    title.add_css_class("setup-title");
    welcome.append(&title);
    let subtitle = gtk::Label::builder()
        .label(crate::i18n::gettext("One place for every package on your system — your distribution, Flatpak and Snap together."))
        .wrap(true)
        .justify(gtk::Justification::Center)
        .halign(gtk::Align::Center)
        .build();
    subtitle.add_css_class("dim-label");
    welcome.append(&subtitle);
    let system = gtk::Box::builder().spacing(8).halign(gtk::Align::Center).margin_bottom(6).build();
    system.append(&util::label(&pretty_name(), &["stat-pill"]));
    for source in 0..ctx.backends.len() {
        system.append(&util::label(ctx.source_name(source), &["stat-pill"]));
    }
    welcome.append(&system);
    welcome.append(&feature("system-software-install-symbolic", "Discover and search everywhere", "Browse apps with sizes and sources, or search every package manager at once."));
    welcome.append(&feature("software-update-available-symbolic", "Updates in one click", "See every pending update by source, then update everything or just what you choose."));
    welcome.append(&feature("security-medium-symbolic", "Safe by default", "Every change is previewed first, AUR builds are reviewed, and Flatpak permissions are yours to edit."));
    carousel.append(&scrolled(&welcome));

    // 2. Sources.
    let sources = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(18).margin_top(18).margin_bottom(18).margin_start(24).margin_end(24).build();
    sources.append(&heading(
        "network-server-symbolic",
        "Choose your sources",
        "Your distribution's package manager is detected automatically. Installing another source asks for your password.",
    ));
    let group = adw::PreferencesGroup::new();
    let flatpak_installed = athena_core::distro::flatpak_installed();
    let snap_installed = athena_core::distro::snap_installed();
    let flatpak = source_row(
        "Flatpak",
        "Sandboxed desktop apps from Flathub, with permissions you control.",
        "application-x-executable-symbolic",
        flatpak_installed,
        !athena_core::distro::flatpak_install_commands().is_empty(),
    );
    let snap = source_row(
        "Snap",
        "Canonical's app store. Only shown in Athena when snapd is installed.",
        "application-x-addon-symbolic",
        snap_installed,
        !athena_core::distro::snap_install_commands().is_empty(),
    );
    group.add(&flatpak);
    group.add(&snap);
    if athena_core::distro::detect_family() == athena_core::distro::DistroFamily::Arch
        && let Some(settings) = settings::load()
    {
        let row = adw::SwitchRow::builder()
            .title(crate::i18n::gettext("Arch User Repository"))
            .subtitle(crate::i18n::gettext("Community packages. Every build is reviewed before it runs. Applies after restart."))
            .build();
        let image = gtk::Image::builder().icon_name("system-users-symbolic").valign(gtk::Align::Center).build();
        image.add_css_class("permission-icon");
        row.add_prefix(&image);
        settings.bind("aur-enabled", &row, "active").build();
        group.add(&row);
    }
    sources.append(&group);
    carousel.append(&scrolled(&sources));

    // 3. Preferences.
    let prefs = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(18).margin_top(18).margin_bottom(18).margin_start(24).margin_end(24).build();
    prefs.append(&heading("preferences-system-symbolic", "Make it yours", "A few common choices. Everything else is in Preferences from the main menu."));
    let general = adw::PreferencesGroup::new();
    if let Some(settings) = settings::load() {
        let scheme = adw::ComboRow::builder()
            .title("Appearance")
            .model(&gtk::StringList::new(&["Follow system", "Light", "Dark"]))
            .build();
        let image = gtk::Image::builder().icon_name("weather-clear-night-symbolic").valign(gtk::Align::Center).build();
        image.add_css_class("permission-icon");
        scheme.add_prefix(&image);
        let keys = ["default", "light", "dark"];
        let current = settings.string("color-scheme");
        scheme.set_selected(keys.iter().position(|k| *k == current.as_str()).unwrap_or(0) as u32);
        {
            let settings = settings.clone();
            scheme.connect_selected_notify(move |row| {
                if let Some(key) = keys.get(row.selected() as usize) {
                    let _ = settings.set_string("color-scheme", key);
                    settings::apply(&settings);
                }
            });
        }
        general.add(&scheme);
        for (title, subtitle, key, icon) in [
            ("Close terminal on success", "Hide the progress window when a change finishes cleanly.", "close-on-success", "utilities-terminal-symbolic"),
            ("Check for updates in the background", "Get notified about pending updates while Athena is closed.", "background-updates", "software-update-available-symbolic"),
            ("Check for Athena releases", "Show a banner when a new version of Athena is out.", "check-releases", "emblem-synchronizing-symbolic"),
        ] {
            let row = adw::SwitchRow::builder().title(title).subtitle(subtitle).build();
            let image = gtk::Image::builder().icon_name(icon).valign(gtk::Align::Center).build();
            image.add_css_class("permission-icon");
            row.add_prefix(&image);
            settings.bind(key, &row, "active").build();
            general.add(&row);
        }
    }
    prefs.append(&general);
    carousel.append(&scrolled(&prefs));

    // 4. Summary.
    let summary = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(18).margin_top(18).margin_bottom(18).margin_start(24).margin_end(24).build();
    summary.append(&heading("emblem-ok-symbolic", "Ready to go", "Finish applies these choices. Anything that installs software asks for your password first."));
    let changes = adw::PreferencesGroup::new();
    summary.append(&changes);
    carousel.append(&scrolled(&summary));

    // Bottom bar: Back, page dots, Next / Finish.
    let back = gtk::Button::builder().label(crate::i18n::gettext("Back")).sensitive(false).build();
    let next = gtk::Button::builder().label(crate::i18n::gettext("Get Started")).build();
    next.add_css_class("suggested-action");
    next.add_css_class("pill");
    back.add_css_class("pill");
    let dots = adw::CarouselIndicatorDots::builder().carousel(&carousel).hexpand(true).build();
    let bar = gtk::Box::builder().spacing(12).margin_top(8).margin_bottom(16).margin_start(20).margin_end(20).build();
    bar.append(&back);
    bar.append(&dots);
    bar.append(&next);

    let toolbar = adw::ToolbarView::new();
    toolbar.add_top_bar(&adw::HeaderBar::builder().show_title(false).build());
    toolbar.set_content(Some(&carousel));
    toolbar.add_bottom_bar(&bar);
    dialog.set_child(Some(&toolbar));

    let refresh_summary = {
        let (flatpak, snap) = (flatpak.clone(), snap.clone());
        move || {
            let rows: Vec<(String, String, &str)> = [
                ("Flatpak", flatpak.is_active(), flatpak_installed),
                ("Snap", snap.is_active(), snap_installed),
            ]
            .into_iter()
            .map(|(name, wanted, present)| {
                let (state, icon) = match (wanted, present) {
                    (true, true) => ("Already set up", "emblem-ok-symbolic"),
                    (true, false) => ("Will be installed", "list-add-symbolic"),
                    (false, true) => ("Will be removed", "list-remove-symbolic"),
                    (false, false) => ("Not used", "action-unavailable-symbolic"),
                };
                (name.to_owned(), state.to_owned(), icon)
            })
            .collect();
            rows
        }
    };
    let summary_rows: Rc<std::cell::RefCell<Vec<adw::ActionRow>>> = Rc::default();
    let update_summary = {
        let (changes, summary_rows) = (changes.clone(), summary_rows.clone());
        move || {
            for row in summary_rows.borrow_mut().drain(..) {
                changes.remove(&row);
            }
            for (name, state, icon) in refresh_summary() {
                let row = adw::ActionRow::builder().title(name).subtitle(state).build();
                let image = gtk::Image::builder().icon_name(icon).valign(gtk::Align::Center).build();
                image.add_css_class("permission-icon");
                row.add_prefix(&image);
                changes.add(&row);
                summary_rows.borrow_mut().push(row);
            }
            let restart = adw::ActionRow::builder().title("Restart Athena").subtitle("New sources are detected the next time Athena starts.").build();
            let image = gtk::Image::builder().icon_name("view-refresh-symbolic").valign(gtk::Align::Center).build();
            image.add_css_class("permission-icon");
            restart.add_prefix(&image);
            changes.add(&restart);
            summary_rows.borrow_mut().push(restart);
        }
    };

    let finish = {
        let (ctx, dialog) = (ctx.clone(), dialog.clone());
        move || {
            let mut commands = Vec::new();
            for (wanted, installed, install, remove) in [
                (
                    flatpak.is_active(),
                    flatpak_installed,
                    athena_core::distro::flatpak_install_commands(),
                    athena_core::distro::flatpak_remove_commands(),
                ),
                (
                    snap.is_active(),
                    snap_installed,
                    athena_core::distro::snap_install_commands(),
                    athena_core::distro::snap_remove_commands(),
                ),
            ] {
                if wanted != installed {
                    commands.extend(if wanted { install } else { remove });
                }
            }
            if commands.is_empty() {
                if let Some(settings) = settings::load() {
                    let _ = settings.set_boolean("setup-complete", true);
                }
                dialog.close();
            } else {
                let dialog = dialog.clone();
                let ctx2 = ctx.clone();
                crate::terminal::run(&ctx.window, "Configure package sources", commands, true, move |ok| {
                    if ok {
                        if let Some(settings) = settings::load() {
                            let _ = settings.set_boolean("setup-complete", true);
                        }
                        dialog.close();
                        ctx2.toast(&crate::i18n::gettext("Sources configured. Restart Athena to detect the new sources."));
                    }
                });
            }
        }
    };

    {
        let (back, next) = (back.clone(), next.clone());
        carousel.connect_page_changed(move |carousel, index| {
            let last = index + 1 >= carousel.n_pages();
            back.set_sensitive(index > 0);
            next.set_label(&crate::i18n::gettext(match index {
                0 => "Get Started",
                _ if last => "Finish Setup",
                _ => "Continue",
            }));
            if last {
                update_summary();
            }
            let page = carousel.nth_page(index);
            util::animate(&page, "rise-in");
        });
    }
    {
        let carousel = carousel.clone();
        back.connect_clicked(move |_| {
            let index = carousel.position().round() as u32;
            if index > 0 {
                carousel.scroll_to(&carousel.nth_page(index - 1), true);
            }
        });
    }
    {
        let carousel = carousel.clone();
        next.connect_clicked(move |_| {
            let index = carousel.position().round() as u32;
            if index + 1 < carousel.n_pages() {
                carousel.scroll_to(&carousel.nth_page(index + 1), true);
            } else {
                finish();
            }
        });
    }
    dialog.present(Some(&ctx.window));
}
