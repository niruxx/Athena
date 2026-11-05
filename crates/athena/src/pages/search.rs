//! Searching every source at once; results arrive source by source.

use std::cell::Cell;
use std::rc::Rc;

use adw::prelude::*;

use crate::context::{Context, Item};
use crate::package_list::PackageList;
use crate::util;

pub struct SearchPage {
    pub widget: adw::ToolbarView,
    pub search: gtk::SearchEntry,
    list: Rc<PackageList>,
    stack: gtk::Stack,
    title: adw::WindowTitle,
    ctx: Rc<Context>,
    /// Which search is current, so late results of an older one are dropped.
    generation: Cell<u64>,
    pending: Cell<usize>,
    results: std::cell::RefCell<Vec<Item>>,
    last_query: std::cell::RefCell<String>,
    groups_mode: gtk::ToggleButton,
    query_kind: Cell<u32>,
}

impl SearchPage {
    pub fn new(ctx: &Rc<Context>) -> Rc<Self> {
        let title = adw::WindowTitle::new(&crate::i18n::gettext("Search"), "");
        let header = adw::HeaderBar::builder().title_widget(&title).build();

        let search = gtk::SearchEntry::builder()
            .placeholder_text("Search packages and apps in every source")
            .hexpand(true)
            .build();
        let packages_mode=gtk::ToggleButton::builder().child(&adw::ButtonContent::builder().label(crate::i18n::gettext("Packages & apps")).icon_name("package-x-generic-symbolic").build()).build();
        let groups_mode=gtk::ToggleButton::builder().child(&adw::ButtonContent::builder().label(crate::i18n::gettext("Groups")).icon_name("folder-symbolic").build()).build();groups_mode.set_group(Some(&packages_mode));packages_mode.set_active(true);
        for mode in [&packages_mode,&groups_mode]{mode.add_css_class("filter-chip");}
        let modes=gtk::Box::builder().spacing(6).build();modes.append(&packages_mode);modes.append(&groups_mode);
        let advanced=gtk::MenuButton::builder().icon_name(util::themed_icon(&["view-filter-symbolic","preferences-system-symbolic"])).tooltip_text("More search filters").build();
        let fields=gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(12).margin_top(18).margin_bottom(18).margin_start(18).margin_end(18).width_request(260).build();
        let query_kind=gtk::DropDown::from_strings(&["Package name or description","Provides capability","Requires capability"]);
        let repository=gtk::Entry::builder().placeholder_text("Repository").build();let architecture=gtk::Entry::builder().placeholder_text("Architecture").build();
        fields.append(&util::label("Search filters", &["heading"]));fields.append(&query_kind);fields.append(&repository);fields.append(&architecture);advanced.set_popover(Some(&gtk::Popover::builder().child(&fields).build()));
        let bar = gtk::Box::builder()
            .orientation(gtk::Orientation::Vertical)
            .spacing(10)
            .margin_top(10)
            .margin_bottom(12)
            .margin_start(18)
            .margin_end(18)
            .build();
        bar.add_css_class("search-hero");
        let input=gtk::Box::builder().spacing(10).build();input.append(&search);bar.append(&input);
        let options=gtk::Box::builder().spacing(10).build();options.add_css_class("filter-bar");options.append(&modes);let spacer=gtk::Box::builder().hexpand(true).build();options.append(&spacer);bar.append(&options);
        let search_bar = adw::Clamp::builder().maximum_size(1000).child(&bar).build();

        let list = Rc::new(PackageList::new(ctx));
        let stack = util::animated_stack();
        let start = adw::StatusPage::builder()
            .icon_name("system-search-symbolic")
            .title(crate::i18n::gettext("Search Your Sources"))
            .description(format!(
                "Search by name, description, or capability across {}.",
                (0..ctx.backends.len()).map(|i| ctx.source_name(i)).collect::<Vec<_>>().join(", ")
            ))
            .build();
        start.add_css_class("page-enter");
        let start_body = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(18).halign(gtk::Align::Center).build();
        let suggestions = gtk::FlowBox::builder().selection_mode(gtk::SelectionMode::None).column_spacing(8).row_spacing(8).max_children_per_line(6).halign(gtk::Align::Center).build();
        for term in ["firefox", "vlc", "gimp", "steam", "code", "obs"] {
            let chip = gtk::Button::with_label(term);
            chip.add_css_class("suggestion-chip");
            let entry = search.clone();
            chip.connect_clicked(move |_| { entry.set_text(term); entry.set_position(-1); entry.grab_focus(); });
            suggestions.insert(&chip, -1);
        }
        start_body.append(&util::label("Try", &["dim-label", "caption-heading"]));
        start_body.append(&suggestions);
        let browse_groups = gtk::Button::builder().child(&adw::ButtonContent::builder().label("Browse package groups").icon_name("folder-symbolic").build()).halign(gtk::Align::Center).build();
        browse_groups.add_css_class("pill");
        { let groups_mode = groups_mode.clone(); browse_groups.connect_clicked(move |_| groups_mode.set_active(true)); }
        if ctx.backends.iter().any(|b| matches!(b.key(), "pacman" | "apt" | "dnf")) { start_body.append(&browse_groups); }
        start.set_child(Some(&start_body));
        stack.add_named(&start, Some("start"));
        stack.add_named(&crate::pages::groups::content(ctx,&search),Some("groups"));
        stack.add_named(&util::loading_page("Searching…"), Some("loading"));
        stack.add_named(&list.widget, Some("list"));
        stack.add_named(
            &adw::StatusPage::builder()
                .icon_name("edit-find-symbolic")
                .title(crate::i18n::gettext("No Results"))
                .description(crate::i18n::gettext("Try another word, or another source."))
                .build(),
            Some("empty"),
        );

        let widget = adw::ToolbarView::new();
        widget.add_top_bar(&header);
        widget.add_top_bar(&search_bar);
        widget.set_content(Some(&stack));

        let page = Rc::new(Self {
            widget,
            search: search.clone(),
            list,
            stack,
            title,
            ctx: ctx.clone(),
            generation: Cell::new(0),
            pending: Cell::new(0),
            results: Default::default(),
            last_query: Default::default(),
            groups_mode: groups_mode.clone(),
            query_kind: Cell::new(0),
        });
        {
            let page = page.clone();
            search.connect_activate(move |entry| page.run(&entry.text(), false));
        }
        {
            // Search as you type, once typing pauses.
            let page = page.clone();
            search.set_search_delay(500);
            search.connect_search_changed(move |entry| page.run(&entry.text(), false));
        }
        {
            let weak = Rc::downgrade(&page);
            ctx.on_packages_changed(move || {
                if let Some(page) = weak.upgrade() {
                    let text = page.search.text();
                    if !text.is_empty() {
                        page.run(&text, true);
                    }
                }
            });
        }
        let weak=Rc::downgrade(&page);let sources=util::source_dropdown(ctx,move |source|{if let Some(page)=weak.upgrade(){page.list.set_source(source);page.update_view();}});options.append(&sources);options.append(&advanced);
        {let page=page.clone();groups_mode.connect_toggled(move |toggle|{let group=toggle.is_active();sources.set_visible(!group);advanced.set_sensitive(!group);page.search.set_placeholder_text(Some(if group{"Search package groups"}else{"Search packages and apps in every source"}));page.generation.set(page.generation.get()+1);page.pending.set(0);if !group{page.run(&page.search.text(),true);}page.update_view();});}
        {let page=page.clone();query_kind.connect_selected_notify(move |choice|{page.query_kind.set(choice.selected());page.run(&page.search.text(),true);});}
        {let page=page.clone();repository.connect_changed(move |entry|{page.list.set_repository(&entry.text());page.update_view();});}
        {let page=page.clone();architecture.connect_changed(move |entry|{page.list.set_architecture(&entry.text());page.update_view();});}
        page
    }
    pub fn show_groups(&self){self.groups_mode.set_active(true);}

    fn update_view(&self) {
        if self.groups_mode.is_active(){self.stack.set_visible_child_name("groups");self.title.set_subtitle("Browse package collections and installed members");return;}

        let shown = self.list.visible_count();
        let searching = self.pending.get() > 0;
        let name = if self.search.text().trim().is_empty() {
            "start"
        } else if shown > 0 {
            "list"
        } else if searching {
            "loading"
        } else {
            "empty"
        };
        if self.stack.visible_child_name().as_deref() != Some(name) && name == "list" { util::animate(&self.list.widget, "page-enter"); }
        self.stack.set_visible_child_name(name);
        self.title.set_subtitle(&if searching && shown > 0 {
            format!("{shown} so far, searching {} more source(s)…", self.pending.get())
        } else if shown > 0 {
            format!("{shown} results")
        } else {
            String::new()
        });
    }

    /// Searches every source; `force` repeats the current search (after a
    /// transaction changed what's installed).
    fn run(self: &Rc<Self>, text: &str, force: bool) {
        if self.groups_mode.is_active(){self.update_view();return;}
        let kind=self.query_kind.get();
        let query = text.trim().to_owned();
        if !force && *self.last_query.borrow() == query {
            return;
        }
        *self.last_query.borrow_mut() = query.clone();
        let generation = self.generation.get() + 1;
        self.generation.set(generation);
        self.results.borrow_mut().clear();
        self.list.set_items(Vec::new());
        if query.is_empty() {
            self.pending.set(0);
            self.update_view();
            return;
        }
        self.pending.set(self.ctx.backends.len());
        self.update_view();
        for (source, backend) in self.ctx.backends.iter().enumerate() {
            let (backend, query, page) = (backend.clone(), query.clone(), self.clone());
            let needle = query.to_lowercase();
            util::background(
                move || if kind==0 { backend.search(&query) } else { backend.dependency_query(&query,kind==2) },
                move |packages| {
                    if page.generation.get() != generation {
                        return;
                    }
                    page.pending.set(page.pending.get().saturating_sub(1));
                    let mut results = page.results.borrow_mut();
                    results.extend(packages.into_iter().map(|package| Item { source, package }));
                    // Exact names first, then names that start with it, then the rest.
                    results.sort_by_key(|item| {
                        let name = item.package.name.to_lowercase();
                        (
                            if name == needle {
                                0
                            } else if name.starts_with(&needle) {
                                1
                            } else if name.contains(&needle) {
                                2
                            } else {
                                3
                            },
                            name,
                        )
                    });
                    page.list.set_items(results.clone());
                    drop(results);
                    page.update_view();
                },
            );
        }
    }
}
