//! Everything installed, from every source, filterable.

use std::rc::Rc;

use adw::prelude::*;
use athena_core::PackageInfo;

use crate::context::{Context, Item};
use crate::package_list::{self, PackageList};
use crate::util;

pub struct InstalledPage {
    pub widget: adw::ToolbarView,
    pub search: gtk::SearchEntry,
    list: Rc<PackageList>,
    stack: gtk::Stack,
    title: adw::WindowTitle,
    ctx: Rc<Context>,
}

impl InstalledPage {
    pub fn new(ctx: &Rc<Context>) -> Rc<Self> {
        let title = adw::WindowTitle::new("Installed", "");
        let header = adw::HeaderBar::builder().title_widget(&title).build();
        let refresh = gtk::Button::builder().icon_name("view-refresh-symbolic").tooltip_text("Reload").build();
        header.pack_end(&refresh);

        let search = gtk::SearchEntry::builder().placeholder_text("Filter installed packages").hexpand(true).build();
        let sources = package_list::source_dropdown(ctx);
        let filters = gtk::Box::builder().spacing(8).margin_top(6).margin_bottom(6).margin_start(12).margin_end(12).build();
        filters.append(&search);
        filters.append(&sources);
        let apps = gtk::ToggleButton::with_label("Apps only");
        filters.append(&apps);
        let filter_bar = adw::Clamp::builder().maximum_size(1000).child(&filters).build();

        let list = Rc::new(PackageList::new(ctx));
        let stack = gtk::Stack::new();
        stack.add_named(&util::loading_page("Loading installed packages…"), Some("loading"));
        stack.add_named(&list.widget, Some("list"));
        stack.add_named(
            &adw::StatusPage::builder().icon_name("edit-find-symbolic").title("No Matches").description("Nothing installed matches the filter.").build(),
            Some("empty"),
        );

        let widget = adw::ToolbarView::new();
        widget.add_top_bar(&header);
        widget.add_top_bar(&filter_bar);
        widget.set_content(Some(&stack));

        let page = Rc::new(Self { widget, search: search.clone(), list, stack, title, ctx: ctx.clone() });
        {
            let page = page.clone();
            search.connect_search_changed(move |entry| {
                page.list.set_text(&entry.text());
                page.update_view();
            });
        }
        {
            let page = page.clone();
            sources.connect_selected_notify(move |dropdown| {
                page.list.set_source(package_list::chosen_source(dropdown));
                page.update_view();
            });
        }
        {
            let page = page.clone();
            refresh.connect_clicked(move |_| page.reload());
        }
        {
            let weak = Rc::downgrade(&page);
            ctx.on_packages_changed(move || {
                if let Some(page) = weak.upgrade() {
                    page.reload();
                }
            });
        }
        {
            let page = page.clone();
            apps.connect_toggled(move |toggle| { page.list.set_apps_only(toggle.is_active()); page.update_view(); });
        }
        page.reload();
        page
    }

    fn update_view(&self) {
        let (shown, total) = (self.list.visible_count(), self.list.total_count());
        self.stack.set_visible_child_name(if shown == 0 && total > 0 { "empty" } else { "list" });
        self.title.set_subtitle(&if shown == total { format!("{total} packages") } else { format!("{shown} of {total} packages") });
    }

    pub fn reload(self: &Rc<Self>) {
        if self.list.total_count() == 0 {
            self.stack.set_visible_child_name("loading");
        }
        let backends = self.ctx.backends.clone();
        let page = self.clone();
        util::background(
            move || backends.iter().map(|b| b.list_installed()).collect::<Vec<Vec<PackageInfo>>>(),
            move |per_source| {
                let mut items: Vec<Item> = per_source
                    .into_iter()
                    .enumerate()
                    .flat_map(|(source, packages)| packages.into_iter().map(move |package| Item { source, package }))
                    .collect();
                items.sort_by_key(|item| item.package.name.to_lowercase());
                page.list.set_items(items);
                page.update_view();
            },
        );
    }
}
