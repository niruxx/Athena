//! Native package groups and their installed members.
use std::rc::Rc;
use adw::prelude::*;
use crate::{actions::{self, Kind}, context::Context, util};

pub fn build(ctx: &Rc<Context>) -> adw::ToolbarView {
    let view = adw::ToolbarView::new();
    view.add_top_bar(&adw::HeaderBar::builder().title_widget(&adw::WindowTitle::new("Groups", "")).build());
    let search = gtk::SearchEntry::builder().placeholder_text("Search groups").build();
    view.add_top_bar(&search);
    let list = gtk::ListBox::builder().selection_mode(gtk::SelectionMode::None).build();
    list.add_css_class("boxed-list");
    let needle = Rc::new(std::cell::RefCell::new(String::new()));
    let query = needle.clone();
    list.set_filter_func(move |row| row.tooltip_text().is_none_or(|text| text.to_lowercase().contains(query.borrow().as_str())));
    let target = list.clone();
    search.connect_search_changed(move |entry| { *needle.borrow_mut() = entry.text().to_lowercase(); target.invalidate_filter(); });
    view.set_content(Some(&gtk::ScrolledWindow::builder().child(&list).build()));
    let backends = ctx.backends.clone();
    let ctx = ctx.clone();
    util::background(move || backends.iter().enumerate().flat_map(|(s,b)| b.list_groups().into_iter().map(move |g| (s,g))).collect::<Vec<_>>(), move |groups| {
        if groups.is_empty() { list.append(&gtk::Label::new(Some("This source has no package groups."))); }
        for (source, group) in groups {
            let row = adw::ActionRow::builder().title(gtk::glib::markup_escape_text(&group.name)).subtitle(if group.installed { "Installed" } else { "Not installed" }).activatable(true).build();
            row.set_tooltip_text(Some(&format!("{} {}", group.name, group.description)));
            let ctx = ctx.clone();
            row.connect_activated(move |_| show(&ctx, source, &group.id, group.is_meta));
            list.append(&row);
        }
    });
    view
}

fn show(ctx: &Rc<Context>, source: usize, id: &str, meta: bool) {
    let view = adw::ToolbarView::new();
    view.add_top_bar(&adw::HeaderBar::new());
    view.set_content(Some(&util::loading_page("Loading group…")));
    ctx.navigation.push(&adw::NavigationPage::builder().title(id).child(&view).build());
    let (backend, id) = (ctx.backends[source].clone(), id.to_owned());
    let ctx = ctx.clone();
    util::background(move || {
        let group = backend.group_details(&id, meta);
        let installed = backend.list_installed().into_iter().map(|p| p.name).collect::<std::collections::HashSet<_>>();
        (group, installed)
    }, move |(group, installed)| {
        let missing = group.packages.iter().filter(|n| !installed.contains(*n)).cloned().collect::<Vec<_>>();
        let present = group.packages.iter().filter(|n| installed.contains(*n)).cloned().collect::<Vec<_>>();
        let body = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(8).margin_start(12).margin_end(12).build();
        body.append(&gtk::Label::builder().label(&group.description).wrap(true).build());
        body.append(&gtk::Label::new(Some(&format!("{} of {} installed", present.len(), group.packages.len()))));
        let buttons = gtk::Box::builder().spacing(8).build();
        for (label, names, kind) in [("Install missing", missing, Kind::Install), ("Remove installed", present, Kind::Remove)] {
            let button = gtk::Button::with_label(label); button.set_sensitive(!names.is_empty());
            let ctx = ctx.clone();
            button.connect_clicked(move |_| actions::transaction(&ctx, source, kind, names.clone()));
            buttons.append(&button);
        }
        body.append(&buttons);
        let search = gtk::SearchEntry::builder().placeholder_text("Search within group").build();
        let filter = gtk::DropDown::from_strings(&["All", "Installed", "Not installed"]);
        body.append(&search); body.append(&filter);
        let list = gtk::ListBox::builder().selection_mode(gtk::SelectionMode::None).build();
        for name in group.packages {
            let yes = installed.contains(&name);
            let row = adw::ActionRow::builder().title(gtk::glib::markup_escape_text(&name)).subtitle(if yes { "Installed" } else { "Not installed" }).build();
            row.set_widget_name(if yes { "installed" } else { "missing" });
            row.set_tooltip_text(Some(&name)); list.append(&row);
        }
        let (entry, dropdown) = (search.clone(), filter.clone());
        list.set_filter_func(move |row| {
            let matches = row.tooltip_text().is_some_and(|n| n.to_lowercase().contains(entry.text().to_lowercase().as_str()));
            matches && match dropdown.selected() { 1 => row.widget_name() == "installed", 2 => row.widget_name() == "missing", _ => true }
        });
        let target = list.clone(); search.connect_search_changed(move |_| target.invalidate_filter());
        let target = list.clone(); filter.connect_selected_notify(move |_| target.invalidate_filter());
        body.append(&list);
        view.set_content(Some(&gtk::ScrolledWindow::builder().child(&body).build()));
    });
}
