//! Native package groups and their installed members.
use crate::{
    actions::{self, Kind},
    context::Context,
    util,
};
use adw::prelude::*;
use std::rc::Rc;

pub fn content(ctx: &Rc<Context>, search: &gtk::SearchEntry) -> gtk::Box {
    let view=gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(16).margin_start(18).margin_end(18).margin_top(4).margin_bottom(18).build();view.add_css_class("page-enter");
    let status=Rc::new(std::cell::Cell::new(0u32));
    let controls=gtk::FlowBox::builder().selection_mode(gtk::SelectionMode::None).column_spacing(8).row_spacing(8).build();
    let all=gtk::ToggleButton::with_label("All groups");all.set_active(true);
    let list = gtk::ListBox::builder().selection_mode(gtk::SelectionMode::None).valign(gtk::Align::Start).build();
    list.add_css_class("boxed-list");
    let needle = Rc::new(std::cell::RefCell::new(String::new()));
    let query = needle.clone();
    let status2 = status.clone();
    list.set_filter_func(move |row| {
        row.tooltip_text().is_none_or(|text| text.to_lowercase().contains(query.borrow().as_str()))
            && match status2.get() {
                1 => row.widget_name() == "installed",
                2 => row.widget_name() == "missing",
                _ => true,
            }
    });
    for (index,label) in ["All groups","Installed","Not installed"].iter().enumerate(){
        let button=if index==0{all.clone()}else{let button=gtk::ToggleButton::with_label(label);button.set_group(Some(&all));button};button.add_css_class("filter-chip");
        let (status,list)=(status.clone(),list.clone());button.connect_toggled(move |button|{if button.is_active(){status.set(index as u32);list.invalidate_filter();}});controls.insert(&button,-1);
    }
    view.append(&adw::Clamp::builder().maximum_size(1000).child(&controls).build());
    let target = list.clone();
    search.connect_search_changed(move |entry| {
        *needle.borrow_mut() = entry.text().to_lowercase();
        target.invalidate_filter();
    });
    let clamp=adw::Clamp::builder().maximum_size(1000).child(&list).build();
    view.append(&gtk::ScrolledWindow::builder().child(&clamp).vexpand(true).hscrollbar_policy(gtk::PolicyType::Never).build());
    load_list(ctx, &list);
    let (weak_ctx, weak_list) = (Rc::downgrade(ctx), list.downgrade());
    ctx.on_packages_changed(move || {
        if let (Some(ctx), Some(list)) = (weak_ctx.upgrade(), weak_list.upgrade()) {
            load_list(&ctx, &list);
        }
    });
    view
}

fn load_list(ctx: &Rc<Context>, list: &gtk::ListBox) {
    while let Some(child) = list.first_child() {
        list.remove(&child);
    }
    let list = list.clone();
    let backends = ctx.backends.clone();
    let ctx = ctx.clone();
    util::background(
        move || {
            backends
                .iter()
                .enumerate()
                .flat_map(|(s, b)| b.list_groups().into_iter().map(move |g| (s, g)))
                .collect::<Vec<_>>()
        },
        move |groups| {
            if groups.is_empty() {
                list.append(&gtk::Label::new(Some("This source has no package groups.")));
            }
            for (source, group) in groups {
                let row = adw::ActionRow::builder()
                    .title(gtk::glib::markup_escape_text(&group.name))
                    .subtitle(gtk::glib::markup_escape_text(&format!("{} · {}{}",ctx.source_name(source),if group.installed { "Installed group" } else { "Available group" },if group.description.is_empty(){String::new()}else{format!(" · {}",group.description)})))
                    .activatable(true)
                    .build();
                let icon=gtk::Image::builder().icon_name(if group.installed{"folder-open-symbolic"}else{"folder-symbolic"}).valign(gtk::Align::Center).build();icon.add_css_class("permission-icon");row.add_prefix(&icon);
                if group.installed{let pill=util::label("Installed",&["app-indicator","installed"]);pill.set_valign(gtk::Align::Center);row.add_suffix(&pill);}
                row.add_suffix(&gtk::Image::from_icon_name("go-next-symbolic"));
                row.set_widget_name(if group.installed { "installed" } else { "missing" });
                row.set_tooltip_text(Some(&format!("{} {}", group.name, group.description)));
                let ctx = ctx.clone();
                row.connect_activated(move |_| show(&ctx, source, &group.id, group.is_meta));
                list.append(&row);
            }
        },
    );
}

fn show(ctx: &Rc<Context>, source: usize, id: &str, meta: bool) {
    let view = adw::ToolbarView::new();
    view.add_top_bar(&adw::HeaderBar::new());
    view.set_content(Some(&util::loading_page("Loading group…")));
    ctx.navigation.push(&adw::NavigationPage::builder().title(id).child(&view).build());
    load_group(ctx, source, id, meta, &view);
    let (weak_ctx, weak_view, id) = (Rc::downgrade(ctx), view.downgrade(), id.to_owned());
    ctx.on_packages_changed(move || {
        if let (Some(ctx), Some(view)) = (weak_ctx.upgrade(), weak_view.upgrade()) {
            load_group(&ctx, source, &id, meta, &view);
        }
    });
}

fn load_group(ctx: &Rc<Context>, source: usize, id: &str, meta: bool, view: &adw::ToolbarView) {
    let view = view.clone();
    let (backend, id, title) = (ctx.backends[source].clone(), id.to_owned(), id.to_owned());
    let ctx = ctx.clone();
    util::background(
        move || {
            let mut group = backend.group_details(&id, meta);
            for child in group.sub_groups.clone() {
                group.packages.extend(backend.group_details(&child, false).packages);
            }
            group.packages.sort();
            group.packages.dedup();
            let installed = backend.list_installed().into_iter().map(|p| p.name).collect::<std::collections::HashSet<_>>();
            (group, installed)
        },
        move |(group, installed)| {
            let missing = group.packages.iter().filter(|n| !installed.contains(*n)).cloned().collect::<Vec<_>>();
            let present = group.packages.iter().filter(|n| installed.contains(*n)).cloned().collect::<Vec<_>>();
            let body = gtk::Box::builder()
                .orientation(gtk::Orientation::Vertical)
                .spacing(16)
                .margin_start(18)
                .margin_end(18)
                .margin_top(18)
                .margin_bottom(24)
                .build();
            body.add_css_class("page-enter");
            let hero = gtk::Box::builder().spacing(18).build();
            hero.add_css_class("identity-card");
            hero.append(&gtk::Image::builder().icon_name("folder-symbolic").pixel_size(48).build());
            let text = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(6).valign(gtk::Align::Center).hexpand(true).build();
            text.append(&util::label(&title, &["identity-title"]));
            if !group.description.is_empty() { let d = util::label(&group.description, &["dim-label"]); d.set_wrap(true); text.append(&d); }
            let stats = gtk::FlowBox::builder().selection_mode(gtk::SelectionMode::None).column_spacing(8).row_spacing(6).max_children_per_line(6).build();
            stats.append(&util::label(&format!("{} of {} installed", present.len(), group.packages.len()), &["stat-pill"]));
            stats.append(&util::label(ctx.source_name(source), &["stat-pill"]));
            text.append(&stats);
            hero.append(&text);
            body.append(&hero);
            let buttons = gtk::Box::builder().spacing(10).build();
            buttons.add_css_class("action-buttons");
            for (label, names, kind) in [("Install missing", missing, Kind::Install), ("Remove installed", present, Kind::Remove)] {
                let button = gtk::Button::with_label(label);
                button.add_css_class("pill");
                if kind == Kind::Install { button.add_css_class("suggested-action"); }
                button.set_sensitive(!names.is_empty());
                let ctx = ctx.clone();
                button.connect_clicked(move |_| actions::transaction(&ctx, source, kind, names.clone()));
                buttons.append(&button);
            }
            body.append(&buttons);
            let search = gtk::SearchEntry::builder().placeholder_text("Search within group").build();
            let filter = gtk::DropDown::from_strings(&["All", "Installed", "Not installed"]);
            let filters = gtk::Box::builder().spacing(10).build();
            search.set_hexpand(true);
            filters.append(&search);
            filters.append(&filter);
            body.append(&filters);
            let list = gtk::ListBox::builder().selection_mode(gtk::SelectionMode::None).build();
            list.add_css_class("boxed-list");
            for name in group.packages {
                let yes = installed.contains(&name);
                let row = adw::ActionRow::builder()
                    .title(gtk::glib::markup_escape_text(&name))
                    .subtitle(if yes { "Installed" } else { "Not installed" })
                    .build();
                row.add_prefix(&gtk::Image::from_icon_name(if yes { "emblem-ok-symbolic" } else { "package-x-generic-symbolic" }));
                row.set_activatable(true);
                let (ctx, package) = (
                    ctx.clone(),
                    athena_core::PackageInfo {
                        name: name.clone(),
                        installed: yes,
                        ..Default::default()
                    },
                );
                row.connect_activated(move |_| {
                    crate::details::show(
                        &ctx,
                        crate::context::Item {
                            source,
                            package: package.clone(),
                        },
                    )
                });
                row.set_widget_name(if yes { "installed" } else { "missing" });
                row.set_tooltip_text(Some(&name));
                list.append(&row);
            }
            let (entry, dropdown) = (search.clone(), filter.clone());
            list.set_filter_func(move |row| {
                let matches = row
                    .tooltip_text()
                    .is_some_and(|n| n.to_lowercase().contains(entry.text().to_lowercase().as_str()));
                matches
                    && match dropdown.selected() {
                        1 => row.widget_name() == "installed",
                        2 => row.widget_name() == "missing",
                        _ => true,
                    }
            });
            let target = list.clone();
            search.connect_search_changed(move |_| target.invalidate_filter());
            let target = list.clone();
            filter.connect_selected_notify(move |_| target.invalidate_filter());
            body.append(&list);
            view.set_content(Some(&gtk::ScrolledWindow::builder().child(&adw::Clamp::builder().maximum_size(1000).child(&body).build()).hscrollbar_policy(gtk::PolicyType::Never).build()));
        },
    );
}
