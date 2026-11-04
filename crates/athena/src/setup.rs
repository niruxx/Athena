//! First-run preferences and optional source installation.
use std::rc::Rc;
use adw::prelude::*;
use crate::{context::Context, settings};

pub fn show(ctx: &Rc<Context>) {
    let dialog = adw::Dialog::builder().title("Welcome to Athena").content_width(560).content_height(520).build();
    let navigation = adw::NavigationView::new();
    dialog.set_child(Some(&navigation));
    let welcome = adw::StatusPage::builder().title("Welcome to Athena").description("Manage packages from your distribution, Flatpak, and Snap in one place.").icon_name(crate::APP_ID).build();
    let next = gtk::Button::with_label("Get Started"); next.add_css_class("suggested-action"); welcome.set_child(Some(&next));
    let first = adw::NavigationPage::builder().title("Welcome").child(&welcome).build();
    navigation.add(&first);

    let sources = adw::PreferencesPage::new();
    let group = adw::PreferencesGroup::builder().title("Sources").description("Your distribution's package manager is detected automatically. Installing another source requires authentication.").build();
    let flatpak = adw::SwitchRow::builder().title("Flatpak support").active(athena_core::distro::flatpak_installed()).build();
    let snap = adw::SwitchRow::builder().title("Snap support").active(athena_core::distro::snap_installed()).build();
    snap.set_sensitive(!athena_core::distro::snap_install_commands().is_empty() || snap.is_active());
    group.add(&flatpak); group.add(&snap);
    if athena_core::distro::detect_family() == athena_core::distro::DistroFamily::Arch {
        group.add(&adw::ActionRow::builder().title("Arch User Repository").subtitle("AUR support and PKGBUILD review are planned for Phase 4.").build());
    }
    sources.add(&group);
    let continue_sources = gtk::Button::with_label("Continue"); group.add(&continue_sources);
    let source_page = page("Sources", &sources);
    let prefs = adw::PreferencesPage::new();
    let general = adw::PreferencesGroup::builder().title("Preferences").build();
    if let Some(settings) = settings::load() {
        for (title, key) in [("Close terminal on success", "close-on-success"), ("Check for Athena releases", "check-releases")] {
            let row = adw::SwitchRow::builder().title(title).build(); settings.bind(key, &row, "active").build(); general.add(&row);
        }
    }
    let continue_prefs = gtk::Button::with_label("Review Setup"); general.add(&continue_prefs); prefs.add(&general);
    let prefs_page = page("Preferences", &prefs);
    let summary = adw::StatusPage::builder().title("Ready to Go").description("Apply your source choices and start using Athena. Preferences can be changed from the main menu.").build();
    let finish = gtk::Button::with_label("Finish Setup"); finish.add_css_class("suggested-action"); summary.set_child(Some(&finish));
    let summary_page = page("Summary", &summary);
    for (button, target) in [(next, source_page), (continue_sources, prefs_page), (continue_prefs, summary_page)] {
        let navigation = navigation.clone(); button.connect_clicked(move |_| navigation.push(&target));
    }
    let (ctx2, dialog2) = (ctx.clone(), dialog.clone());
    finish.connect_clicked(move |_| {
        let mut commands = Vec::new();
        for (wanted, installed, install, remove) in [
            (flatpak.is_active(), athena_core::distro::flatpak_installed(), athena_core::distro::flatpak_install_commands(), athena_core::distro::flatpak_remove_commands()),
            (snap.is_active(), athena_core::distro::snap_installed(), athena_core::distro::snap_install_commands(), athena_core::distro::snap_remove_commands()),
        ] { if wanted != installed { commands.extend(if wanted { install } else { remove }); } }
        if commands.is_empty() {
            if let Some(settings) = settings::load() { let _ = settings.set_boolean("setup-complete", true); }
            dialog2.close();
        } else {
            let dialog = dialog2.clone(); let ctx = ctx2.clone();
            crate::terminal::run(&ctx2.window, "Configure package sources", commands, true, move |ok| {
                if ok {
                    if let Some(settings) = settings::load() { let _ = settings.set_boolean("setup-complete", true); }
                    dialog.close(); ctx.toast("Sources configured. Restart Athena to detect the new sources.");
                }
            });
        }
    });
    dialog.present(Some(&ctx.window));
}

fn page(title: &str, child: &impl IsA<gtk::Widget>) -> adw::NavigationPage {
    let toolbar = adw::ToolbarView::new(); toolbar.add_top_bar(&adw::HeaderBar::new()); toolbar.set_content(Some(child));
    adw::NavigationPage::builder().title(title).child(&toolbar).build()
}
