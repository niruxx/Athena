//! Snap channels, revisions, enabled state, and interface connections.
use crate::{actions, context::Context, util};
use adw::prelude::*;
use athena_core::{Backend, backends::SnapBackend, source_tools};
use std::rc::Rc;

pub fn build(ctx: &Rc<Context>) -> adw::ToolbarView {
    let view = adw::ToolbarView::new();
    let header = adw::HeaderBar::builder()
        .title_widget(&adw::WindowTitle::new(&crate::i18n::gettext("Snap Manage"), ""))
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

fn load(ctx: &Rc<Context>, view: &adw::ToolbarView) {
    view.set_content(Some(&util::loading_page("Loading snaps…")));
    let (ctx, view) = (ctx.clone(), view.clone());
    util::background(
        move || SnapBackend.list_installed(),
        move |apps| {
            let body = adw::PreferencesPage::new();
            body.add_css_class("page-enter");
            let hero = gtk::Box::builder().spacing(18).build();
            hero.add_css_class("identity-card");
            hero.append(&gtk::Image::builder().icon_name(util::themed_icon(&["snap-store", "snapcraft", "application-x-addon-symbolic"])).pixel_size(56).build());
            let text = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(6).valign(gtk::Align::Center).build();
            text.append(&util::label("Snap packages", &["identity-title"]));
            let stats = gtk::FlowBox::builder().selection_mode(gtk::SelectionMode::None).column_spacing(8).row_spacing(6).max_children_per_line(6).build();
            stats.append(&util::label(&format!("{} installed", apps.len()), &["stat-pill"]));
            text.append(&stats);
            hero.append(&text);
            let hero_group = adw::PreferencesGroup::new();
            hero_group.add(&hero);
            body.add(&hero_group);
            let group = adw::PreferencesGroup::builder()
                .title(crate::i18n::gettext("Installed snaps"))
                .description("Choose a snap to switch channels, revert, or manage interfaces.")
                .build();
            if apps.is_empty() {
                let empty = adw::ActionRow::builder().title("No snaps installed").subtitle("Install snaps from Discover or Search.").build();
                empty.add_prefix(&gtk::Image::from_icon_name("system-software-install-symbolic"));
                group.add(&empty);
            }
            for app in apps {
                let mut details = vec![if app.installed_version.is_empty() { "Snap".to_owned() } else { format!("Version {}", app.installed_version) }];
                if !app.repository.is_empty() {
                    details.push(app.repository.clone());
                }
                if !app.size.is_empty() {
                    details.push(app.size.clone());
                }
                let row = adw::ActionRow::builder()
                    .title(gtk::glib::markup_escape_text(&crate::catalog_ui::app_name(&app.name)))
                    .subtitle(gtk::glib::markup_escape_text(&details.join(" · ")))
                    .activatable(true)
                    .build();
                row.add_prefix(&crate::catalog_ui::app_icon(&app.name, 40));
                if !app.description.is_empty() {
                    row.set_tooltip_text(Some(&app.description));
                }
                row.add_suffix(&gtk::Image::from_icon_name("go-next-symbolic"));
                let ctx = ctx.clone();
                row.connect_activated(move |_| show(&ctx, &app.name));
                group.add(&row);
            }
            body.add(&group);
            view.set_content(Some(&body));
        },
    );
}

pub fn show(ctx: &Rc<Context>, name: &str) {
    let view = adw::ToolbarView::new();
    view.add_top_bar(&adw::HeaderBar::new());
    ctx.navigation
        .push(&adw::NavigationPage::builder().title(crate::catalog_ui::app_name(name)).child(&view).build());
    load_app(ctx, name, &view);
    let (weak_ctx, weak_view, id) = (Rc::downgrade(ctx), view.downgrade(), name.to_owned());
    ctx.on_packages_changed(move || {
        if let (Some(ctx), Some(view)) = (weak_ctx.upgrade(), weak_view.upgrade()) {
            load_app(&ctx, &id, &view);
        }
    });
}

fn load_app(ctx: &Rc<Context>, name: &str, view: &adw::ToolbarView) {
    let (id, ctx, view, name) = (name.to_owned(), ctx.clone(), view.clone(), name.to_owned());
    util::background(
        move || (source_tools::snap_info(&id), source_tools::snap_connections(&id)),
        move |(info, connections)| {
            let page = adw::PreferencesPage::new();
            page.add_css_class("page-enter");
            let field = |key: &str| info.lines().find_map(|l| l.strip_prefix(key)).map(|v| v.trim().to_owned()).unwrap_or_default();
            let hero = gtk::Box::builder().spacing(18).build();
            hero.add_css_class("identity-card");
            hero.append(&crate::catalog_ui::app_icon(&name, 64));
            let text = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(6).valign(gtk::Align::Center).hexpand(true).build();
            text.append(&util::label(&crate::catalog_ui::app_name(&name), &["identity-title"]));
            let summary = field("summary:");
            if !summary.is_empty() {
                let label = util::label(&summary, &["dim-label"]);
                label.set_wrap(true);
                text.append(&label);
            }
            let stats = gtk::FlowBox::builder().selection_mode(gtk::SelectionMode::None).column_spacing(8).row_spacing(6).max_children_per_line(6).build();
            for (key, prefix) in [("installed:", ""), ("tracking:", "Channel "), ("publisher:", "By ")] {
                let value = field(key);
                if !value.is_empty() {
                    stats.append(&util::label(&format!("{prefix}{}", value.split_whitespace().next().unwrap_or(&value)), &["stat-pill"]));
                }
            }
            text.append(&stats);
            hero.append(&text);
            let hero_group = adw::PreferencesGroup::new();
            hero_group.add(&hero);
            page.add(&hero_group);
            let group = adw::PreferencesGroup::builder().title("Channel and revisions").build();
            if info.lines().any(|line| line.contains("confinement:") && line.contains("classic"))
                || info.lines().any(|line| line.starts_with("installed:") && line.contains("classic"))
            {
                group.set_description(Some("Classic confinement: this snap can access the host like a traditional application."));
            }
            let channel = adw::EntryRow::builder()
                .title(crate::i18n::gettext("Channel (for example latest/stable)"))
                .build();
            let change = gtk::Button::builder().label(crate::i18n::gettext("Switch…")).valign(gtk::Align::Center).build();
            change.add_css_class("row-action");
            channel.add_suffix(&change);
            group.add(&channel);
            let (ctx2, id) = (ctx.clone(), name.clone());
            change.connect_clicked(move |_| {
                run(
                    &ctx2,
                    "Switch Snap channel",
                    "Refresh this snap from the selected channel?",
                    "channel",
                    &id,
                    &channel.text(),
                )
            });
            for (title, subtitle, label, action, icon) in [
                ("Previous revision", "Roll back to the revision installed before the last refresh.", "Revert…", "revert", "edit-undo-symbolic"),
                ("Enable", "Allow the snap's apps and services to run.", "Enable…", "enable", "media-playback-start-symbolic"),
                ("Disable", "Stop the snap without removing it or its data.", "Disable…", "disable", "media-playback-pause-symbolic"),
            ] {
                let (ctx, id) = (ctx.clone(), name.clone());
                group.add(&util::with_icon(util::action_row(title, subtitle, label, move || run(&ctx, title, "Apply this change to the snap?", action, &id, "")), icon));
            }
            page.add(&group);
            let interfaces = adw::PreferencesGroup::builder()
                .title(crate::i18n::gettext("Interfaces"))
                .description(crate::i18n::gettext("Connecting an interface grants the snap additional host access."))
                .build();
            for connection in connections {
                let row = adw::ActionRow::builder()
                    .title(gtk::glib::markup_escape_text(&connection.interface))
                    .subtitle(gtk::glib::markup_escape_text(&format!("{} → {}", connection.plug, connection.slot)))
                    .build();
                let button = gtk::Button::builder()
                    .label(crate::i18n::gettext("Disconnect…"))
                    .valign(gtk::Align::Center)
                    .sensitive(connection.slot != "-" && connection.plug != "-")
                    .build();
                let ctx = ctx.clone();
                button.connect_clicked(move |_| {
                    run(
                        &ctx,
                        "Disconnect interface",
                        "Disconnect this interface from the snap?",
                        "disconnect",
                        &connection.plug,
                        "",
                    )
                });
                button.add_css_class("row-action");
                row.add_suffix(&button);
                interfaces.add(&util::with_icon(row, if connection.slot == "-" { "network-offline-symbolic" } else { "emblem-ok-symbolic" }));
            }
            let plug = adw::EntryRow::builder().title(crate::i18n::gettext("Plug (snap:plug)")).build();
            let slot = adw::EntryRow::builder().title(crate::i18n::gettext("Slot (snap:slot or :slot)")).build();
            interfaces.add(&plug);
            interfaces.add(&slot);
            let connect = gtk::Button::with_label(&crate::i18n::gettext("Connect…"));
            connect.connect_clicked(move |_| {
                run(
                    &ctx,
                    "Connect interface",
                    "Grant the snap access through this interface?",
                    "connect",
                    &plug.text(),
                    &slot.text(),
                )
            });
            let connect_row = util::with_icon(adw::ActionRow::builder().title("Connect interface").subtitle("Fill in the plug and slot above.").build(), "list-add-symbolic");
            connect.set_valign(gtk::Align::Center);
            connect.add_css_class("row-action");
            connect_row.add_suffix(&connect);
            interfaces.add(&connect_row);
            page.add(&interfaces);
            let raw = adw::PreferencesGroup::builder().title(crate::i18n::gettext("Snap information")).build();
            let details = adw::ExpanderRow::builder().title("Full snap info").subtitle("Raw output of snap info").build();
            let text = gtk::Label::builder().label(&info).selectable(true).wrap(true).xalign(0.0).margin_top(12).margin_bottom(12).margin_start(12).margin_end(12).build();
            text.add_css_class("monospace");
            details.add_row(&text);
            raw.add(&details);
            page.add(&raw);
            view.set_content(Some(&page));
        },
    );
}

fn run(ctx: &Rc<Context>, title: &str, body: &str, action: &str, name: &str, value: &str) {
    if let Some(command) = source_tools::snap_command(action, name, value) {
        actions::custom(ctx, title, body, vec![command]);
    } else {
        ctx.toast(&crate::i18n::gettext("Enter a valid snap, channel, or interface name."));
    }
}
