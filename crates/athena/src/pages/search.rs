//! Searching every source at once; results arrive source by source.

use std::cell::Cell;
use std::rc::Rc;

use adw::prelude::*;

use crate::context::{Context, Item};
use crate::package_list::{self, PackageList};
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
}

impl SearchPage {
    pub fn new(ctx: &Rc<Context>) -> Rc<Self> {
        let title = adw::WindowTitle::new("Search", "");
        let header = adw::HeaderBar::builder().title_widget(&title).build();

        let search = gtk::SearchEntry::builder().placeholder_text("Search packages and apps in every source").hexpand(true).build();
        let sources = package_list::source_dropdown(ctx);
        let bar = gtk::Box::builder().spacing(8).margin_top(6).margin_bottom(6).margin_start(12).margin_end(12).build();
        bar.append(&search);
        bar.append(&sources);
        let search_bar = adw::Clamp::builder().maximum_size(1000).child(&bar).build();

        let list = Rc::new(PackageList::new(ctx));
        let stack = gtk::Stack::new();
        stack.add_named(
            &adw::StatusPage::builder()
                .icon_name("system-search-symbolic")
                .title("Search Your Sources")
                .description(format!(
                    "Finds packages and apps in {}.",
                    (0..ctx.backends.len()).map(|i| ctx.source_name(i)).collect::<Vec<_>>().join(", ")
                ))
                .build(),
            Some("start"),
        );
        stack.add_named(&util::loading_page("Searching…"), Some("loading"));
        stack.add_named(&list.widget, Some("list"));
        stack.add_named(
            &adw::StatusPage::builder().icon_name("edit-find-symbolic").title("No Results").description("Try another word, or another source.").build(),
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
            let page = page.clone();
            sources.connect_selected_notify(move |dropdown| {
                page.list.set_source(package_list::chosen_source(dropdown));
                page.update_view();
            });
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
        page
    }

    fn update_view(&self) {
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
                move || backend.search(&query),
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
                        (if name == needle { 0 } else if name.starts_with(&needle) { 1 } else if name.contains(&needle) { 2 } else { 3 }, name)
                    });
                    page.list.set_items(results.clone());
                    drop(results);
                    page.update_view();
                },
            );
        }
    }
}
