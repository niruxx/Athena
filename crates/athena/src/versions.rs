//! Downgrade chooser, with an explicit transaction confirmation.
use crate::{context::Context, util};
use adw::prelude::*;
use std::rc::Rc;

pub fn show(ctx: &Rc<Context>, name: &str) {
    let dialog = adw::Dialog::builder()
        .title(crate::i18n::gettext("Other Versions"))
        .content_width(600)
        .content_height(500)
        .build();
    let toolbar = adw::ToolbarView::new();
    toolbar.add_top_bar(&adw::HeaderBar::new());
    let body = adw::PreferencesPage::new();
    let group = adw::PreferencesGroup::builder()
        .title(crate::i18n::gettext("Package cache"))
        .description(crate::i18n::gettext("Installing an older version can require matching older dependencies."))
        .build();
    let archive = gtk::Button::with_label(&crate::i18n::gettext("Load Arch Linux Archive Versions"));
    body.add(&group);
    let archive_group = adw::PreferencesGroup::builder().title(crate::i18n::gettext("Arch Linux Archive")).build();
    archive_group.add(&archive);
    body.add(&archive_group);
    toolbar.set_content(Some(&body));
    dialog.set_child(Some(&toolbar));
    let (ctx2, name2, group2, dialog2) = (ctx.clone(), name.to_owned(), archive_group.clone(), dialog.clone());
    archive.connect_clicked(move |button| {
        button.set_sensitive(false);
        button.set_label(&crate::i18n::gettext("Loading archive…"));
        let (ctx, name, group, dialog, button) = (ctx2.clone(), name2.clone(), group2.clone(), dialog2.clone(), button.clone());
        util::background(
            move || athena_core::versions::archive(&name),
            move |versions| {
                button.set_label(if versions.is_empty() {
                    "No archive versions found (check your connection)"
                } else {
                    "Archive loaded"
                });
                add(&ctx, &group, &dialog, versions);
            },
        );
    });
    let (ctx2, name, dialog2) = (ctx.clone(), name.to_owned(), dialog.clone());
    util::background(
        move || athena_core::versions::cached(&name),
        move |versions| {
            if versions.is_empty() {
                group.add(&gtk::Label::new(Some("No cached versions found.")));
            }
            add(&ctx2, &group, &dialog2, versions);
        },
    );
    dialog.present(Some(&ctx.window));
}

fn add(ctx: &Rc<Context>, group: &adw::PreferencesGroup, dialog: &adw::Dialog, mut versions: Vec<athena_core::versions::Version>) {
    versions.sort_by(|a, b| b.version.cmp(&a.version));
    for version in versions {
        let row = adw::ActionRow::builder()
            .title(gtk::glib::markup_escape_text(&version.version))
            .subtitle(gtk::glib::markup_escape_text(&format!("{} · {}", version.architecture, version.location)))
            .build();
        let install = gtk::Button::builder()
            .label(crate::i18n::gettext("Install…"))
            .valign(gtk::Align::Center)
            .build();
        let (ctx, dialog) = (ctx.clone(), dialog.clone());
        install.connect_clicked(move |_| {
            let Some(command) = athena_core::versions::install(&version) else { return };
            dialog.close();
            crate::actions::custom(
                &ctx,
                "Install another package version",
                "Pacman will resolve this package's dependencies. An older version may be incompatible with the rest of the system.",
                vec![command],
            );
        });
        row.add_suffix(&install);
        group.add(&row);
    }
}
