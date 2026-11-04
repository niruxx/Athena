//! Package details loaded away from the GTK thread.
use std::rc::Rc;
use adw::prelude::*;
use gtk::gio;
use crate::{actions::{self, Kind}, context::{Context, Item}, util};

pub fn show(ctx: &Rc<Context>, item: Item) {
    let toolbar = adw::ToolbarView::new();
    toolbar.add_top_bar(&adw::HeaderBar::new());
    toolbar.set_content(Some(&util::loading_page("Loading package details…")));
    let page = adw::NavigationPage::builder().title(&item.package.name).child(&toolbar).build();
    ctx.navigation.push(&page);
    let backend = ctx.backends[item.source].clone();
    let name = item.package.name.clone();
    let ctx = ctx.clone();
    util::background(move || backend.package_details(&[name]), move |packages| {
        let package = packages.into_iter().next().unwrap_or(item.package);
        let body = adw::PreferencesPage::new();
        let group = adw::PreferencesGroup::builder().title(&package.name).description(&package.description).build();
        let buttons = gtk::Box::builder().spacing(8).halign(gtk::Align::Center).margin_top(12).margin_bottom(12).build();
        for (label, kind) in if package.installed {
            vec![("Update", Kind::Update), ("Reinstall", Kind::Reinstall), ("Remove…", Kind::Remove)]
        } else { vec![("Install", Kind::Install)] } {
            let button = gtk::Button::with_label(label);
            let (ctx, name) = (ctx.clone(), package.name.clone());
            button.connect_clicked(move |_| actions::transaction(&ctx, item.source, kind, vec![name.clone()]));
            buttons.append(&button);
        }
        if let Some(app) = crate::desktop::find(&package.name) {
            let open = gtk::Button::with_label("Open");
            let ctx = ctx.clone();
            open.connect_clicked(move |_| { if let Err(error) = app.launch(&[], None::<&gio::AppLaunchContext>) { ctx.toast(&error.to_string()); } });
            buttons.append(&open);
        }
        group.add(&buttons);
        for (title, value) in [("Description", &package.long_description), ("Installed version", &package.installed_version), ("Available version", &package.available_version), ("Architecture", &package.architecture), ("Size", &package.size), ("Repository", &package.repository)] {
            if !value.is_empty() {
                group.add(&adw::ActionRow::builder().title(title).subtitle(gtk::glib::markup_escape_text(value)).subtitle_selectable(true).build());
            }
        }
        if package.homepage_url.starts_with("https://") || package.homepage_url.starts_with("http://") {
            group.add(&gtk::LinkButton::with_label(&package.homepage_url, "Homepage"));
        }
        group.add(&adw::ActionRow::builder().title("Source").subtitle(ctx.source_name(item.source)).build());
        body.add(&group);
        if ctx.backends[item.source].key() == "flatpak" && package.installed {
            let permissions = adw::PreferencesGroup::builder().title("Sandbox permissions").build();
            let target = permissions.clone();
            let name = package.name.clone();
            util::background(move || athena_core::backends::FlatpakBackend::permissions(&name), move |p| {
                for (title, values) in [("Network and shared resources", p.shared.into_iter().collect::<Vec<_>>()), ("Sockets", p.sockets.into_iter().collect()), ("Devices", p.devices.into_iter().collect()), ("Files", p.filesystems), ("Session bus", p.session_bus_talk), ("System bus", p.system_bus_talk)] {
                    if values.is_empty() { continue; }
                    let row = adw::ActionRow::builder().title(title).subtitle(gtk::glib::markup_escape_text(&values.join(", "))).build();
                    if values.iter().any(|v| matches!(v.as_str(), "host" | "home" | "all" | "system-bus" | "session-bus" | "x11")) { row.add_css_class("warning"); }
                    target.add(&row);
                }
            });
            let edit = gtk::Button::with_label("Edit Permissions");
            let (ctx2, name) = (ctx.clone(), package.name.clone());
            edit.connect_clicked(move |_| crate::permissions::show(&ctx2, &name));
            permissions.add(&edit);
            body.add(&permissions);
        }
        toolbar.set_content(Some(&body));
    });
}
