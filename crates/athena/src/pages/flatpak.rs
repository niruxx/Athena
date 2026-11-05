//! Installed Flatpak apps and leftovers, with data and permission controls.
use crate::{context::Context, util};
use adw::prelude::*;
use std::rc::Rc;

pub fn build(ctx: &Rc<Context>) -> adw::ToolbarView {
    let view = adw::ToolbarView::new();
    let header = adw::HeaderBar::builder()
        .title_widget(&adw::WindowTitle::new(
            &crate::i18n::gettext("Flatpak"),
            &crate::i18n::gettext("Applications and leftovers"),
        ))
        .build();
    let refresh = gtk::Button::builder()
        .icon_name("view-refresh-symbolic")
        .tooltip_text(crate::i18n::gettext("Reload"))
        .build();
    header.pack_end(&refresh);
    view.add_top_bar(&header);
    let (ctx2, target) = (ctx.clone(), view.clone());
    refresh.connect_clicked(move |_| load(&ctx2, &target));
    let (weak_ctx, weak_view) = (Rc::downgrade(ctx), view.downgrade());
    ctx.on_packages_changed(move || {
        if let (Some(ctx), Some(view)) = (weak_ctx.upgrade(), weak_view.upgrade()) {
            load(&ctx, &view);
        }
    });
    load(ctx, &view);
    view
}

pub fn show_app(ctx: &Rc<Context>, app: &str) {
    let view = adw::ToolbarView::new();
    view.add_top_bar(&adw::HeaderBar::new());
    ctx.navigation
        .push(&adw::NavigationPage::builder().title(crate::catalog_ui::app_name(app)).child(&view).build());
    let stack = gtk::Stack::builder().transition_type(gtk::StackTransitionType::SlideLeftRight).transition_duration(260).build();
    stack.add_titled(&crate::permissions::editor(ctx, app), Some("permissions"), "Permissions");
    let page = adw::PreferencesPage::new();
    let group = adw::PreferencesGroup::builder().title(crate::i18n::gettext("Application data")).build();
    let (id, ctx2, target) = (app.to_owned(), ctx.clone(), group.clone());
    util::background(
        move || athena_core::source_tools::flatpak_data_for(&id),
        move |result| {
            if let Ok(app) = result {
                add_data(&ctx2, &target, &app, None);
            }
        },
    );
    page.add(&group);
    stack.add_titled(&page, Some("data"), "App Data");
    view.add_top_bar(&gtk::StackSwitcher::builder().stack(&stack).halign(gtk::Align::Center).margin_top(6).margin_bottom(6).build());
    view.set_content(Some(&stack));
}

fn load(ctx: &Rc<Context>, view: &adw::ToolbarView) {
    view.set_content(Some(&util::loading_page("Measuring Flatpak data…")));
    let (ctx, view) = (ctx.clone(), view.clone());
    util::background(athena_core::source_tools::flatpak_data, move |apps| {
        let body = adw::PreferencesPage::new();
        body.add_css_class("page-enter");
        let installed_apps: Vec<_> = apps.iter().filter(|a| a.installed).collect();
        let total: u64 = apps.iter().map(|a| a.bytes).sum();
        let hero = gtk::Box::builder().spacing(18).build();
        hero.add_css_class("identity-card");
        hero.append(&gtk::Image::builder().icon_name(util::themed_icon(&["org.flatpak.Flatpak", "flatpak", "application-x-executable-symbolic"])).pixel_size(56).build());
        let text = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(6).valign(gtk::Align::Center).hexpand(true).build();
        text.append(&util::label("Flatpak applications", &["identity-title"]));
        let stats = gtk::FlowBox::builder().selection_mode(gtk::SelectionMode::None).column_spacing(8).row_spacing(6).max_children_per_line(6).build();
        stats.append(&util::label(&format!("{} installed", installed_apps.len()), &["stat-pill"]));
        stats.append(&util::label(&format!("{} of app data", athena_core::backends::human_size(total)), &["stat-pill"]));
        let overrides = apps.iter().filter(|a| a.has_override).count();
        if overrides > 0 {
            stats.append(&util::label(&format!("{overrides} with custom permissions"), &["stat-pill"]));
        }
        text.append(&stats);
        hero.append(&text);
        let hero_group = adw::PreferencesGroup::new();
        hero_group.add(&hero);
        body.add(&hero_group);
        for installed in [true, false] {
            let group = adw::PreferencesGroup::builder()
                .title(if installed {
                    "Installed applications"
                } else {
                    "Leftovers from removed applications"
                })
                .build();
            let mut count = 0;
            for app in apps.iter().filter(|a| a.installed == installed) {
                add_data(&ctx, &group, app, Some(&view));
                count += 1;
            }
            if count == 0 {
                let empty = adw::ActionRow::builder()
                    .title(if installed { "No Flatpak applications installed" } else { "No leftovers found" })
                    .subtitle(if installed { "Install apps from Discover or Search." } else { "Removed apps left nothing behind." })
                    .build();
                empty.add_prefix(&gtk::Image::from_icon_name(if installed { "system-software-install-symbolic" } else { "user-trash-symbolic" }));
                group.add(&empty);
            } else {
                group.set_description(Some(&format!("{count} item{}", if count == 1 { "" } else { "s" })));
            }
            body.add(&group);
        }
        view.set_content(Some(&body));
    });
}

fn add_data(ctx: &Rc<Context>, group: &adw::PreferencesGroup, app: &athena_core::source_tools::FlatpakData, view: Option<&adw::ToolbarView>) {
    let name = crate::catalog_ui::app_name(&app.app);
    let row = adw::ExpanderRow::builder()
        .title(gtk::glib::markup_escape_text(&name))
        .subtitle(gtk::glib::markup_escape_text(&format!(
            "{}{} · {} data{}",
            if name != app.app { &app.app } else { "" },
            if name != app.app { "" } else { "Flatpak" },
            athena_core::backends::human_size(app.bytes),
            if app.has_override { " · Custom permissions" } else { "" }
        )))
        .build();
    let icon = crate::catalog_ui::app_icon(&app.app, 40);
    if !app.installed {
        icon.set_opacity(0.55);
    }
    row.add_prefix(&icon);
    if app.has_override {
        let pill = util::label("Overrides", &["app-indicator", "accent"]);
        pill.set_valign(gtk::Align::Center);
        row.add_suffix(&pill);
    }
    if app.installed {
        let (ctx, id) = (ctx.clone(), app.app.clone());
        let edit = util::with_icon(
            util::action_row("Permissions", "Review and change what this app can reach outside its sandbox.", "Edit…", move || crate::pages::flatpak::show_app(&ctx, &id)),
            "security-medium-symbolic",
        );
        row.add_row(&edit);
    }
    let path = app.path.clone();
    let window = ctx.window.clone();
    let open = util::with_icon(
        util::action_row("Data folder", &app.path.display().to_string(), "Open", move || {
            gtk::FileLauncher::new(Some(&gtk::gio::File::for_path(&path))).launch(Some(&window), None::<&gtk::gio::Cancellable>, |_| {})
        }),
        "folder-symbolic",
    );
    open.set_sensitive(app.has_data);
    row.add_row(&open);
    let clear = gtk::Button::with_label(if app.installed { "Clear…" } else { "Remove…" });
    clear.add_css_class("destructive-action");
    clear.set_sensitive(app.has_data || app.has_override);
    let (ctx, id, leftovers, view) = (ctx.clone(), app.app.clone(), !app.installed, view.cloned());
    clear.connect_clicked(move |_| {
        let dialog = adw::AlertDialog::new(
            Some(&crate::i18n::gettext("Delete application data?")),
            Some("This permanently removes this app's saved files and settings. Close the app first. Leftover user overrides will also be reset."),
        );
        dialog.add_responses(&[("cancel", &crate::i18n::gettext("Cancel")), ("delete", &crate::i18n::gettext("Delete"))]);
        dialog.set_response_appearance("delete", adw::ResponseAppearance::Destructive);
        dialog.set_close_response("cancel");
        let (ctx2, id, view) = (ctx.clone(), id.clone(), view.clone());
        dialog.connect_response(Some("delete"), move |_, _| {
            let (ctx, id, view) = (ctx2.clone(), id.clone(), view.clone());
            util::background(
                move || athena_core::source_tools::clear_flatpak_data(&id, leftovers),
                move |result| match result {
                    Ok(()) => {
                        ctx.toast(&crate::i18n::gettext("Application data removed."));
                        if let Some(view) = view {
                            load(&ctx, &view);
                        }
                    }
                    Err(error) => ctx.toast(&error),
                },
            );
        });
        dialog.present(Some(&ctx.window));
    });
    let clear_row = adw::ActionRow::builder()
        .title(if app.installed { "Clear data" } else { "Remove leftovers" })
        .subtitle("Permanently delete saved files and settings.")
        .build();
    clear.set_valign(gtk::Align::Center);
    clear.add_css_class("row-action");
    clear_row.add_suffix(&clear);
    let clear_row = util::with_icon(clear_row, "user-trash-symbolic");
    row.add_row(&clear_row);
    group.add(&row);
}
