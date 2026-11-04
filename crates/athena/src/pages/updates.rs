//! Pending updates from every source, grouped by source.

use std::cell::RefCell;
use std::rc::Rc;

use adw::prelude::*;
use athena_core::PackageInfo;

use crate::actions::{self, Kind};
use crate::context::Context;
use crate::util;

type CountListener = Box<dyn Fn(usize)>;

pub struct UpdatesPage {
    pub widget: adw::ToolbarView,
    stack: gtk::Stack,
    content: adw::PreferencesPage,
    groups: RefCell<Vec<adw::PreferencesGroup>>,
    update_all: gtk::Button,
    summary: adw::ActionRow,
    title: adw::WindowTitle,
    counts: RefCell<Vec<(usize, usize)>>,
    on_count: RefCell<Option<CountListener>>,
    ctx: Rc<Context>,
}

impl UpdatesPage {
    pub fn new(ctx: &Rc<Context>) -> Rc<Self> {
        let title = adw::WindowTitle::new("Updates", "");
        let header = adw::HeaderBar::builder().title_widget(&title).build();
        let refresh = gtk::Button::builder().icon_name("view-refresh-symbolic").tooltip_text("Check for updates").build();
        header.pack_start(&refresh);

        // A summary at the top: how many, from where, and Update Everything.
        let update_all = gtk::Button::builder().label("Update Everything").valign(gtk::Align::Center).build();
        update_all.add_css_class("suggested-action");
        update_all.add_css_class("pill");
        let summary = adw::ActionRow::builder().title("Updates available").build();
        summary.add_prefix(&gtk::Image::builder().icon_name("software-update-available-symbolic").pixel_size(32).build());
        summary.add_suffix(&update_all);
        let summary_group = adw::PreferencesGroup::new();
        summary_group.add(&summary);

        let content = adw::PreferencesPage::new();
        content.add(&summary_group);
        let stack = gtk::Stack::new();
        stack.add_named(&util::loading_page("Checking for updates…"), Some("loading"));
        stack.add_named(&content, Some("list"));
        stack.add_named(
            &adw::StatusPage::builder()
                .icon_name("emblem-ok-symbolic")
                .title("Up to Date")
                .description("Every source is up to date.")
                .build(),
            Some("none"),
        );

        let widget = adw::ToolbarView::new();
        widget.add_top_bar(&header);
        widget.set_content(Some(&stack));

        let page = Rc::new(Self {
            widget,
            stack,
            content,
            groups: RefCell::default(),
            update_all: update_all.clone(),
            summary,
            title,
            counts: RefCell::default(),
            on_count: RefCell::default(),
            ctx: ctx.clone(),
        });
        {
            let page = page.clone();
            refresh.connect_clicked(move |_| page.reload());
        }
        {
            let page = page.clone();
            update_all.connect_clicked(move |_| actions::update_everything(&page.ctx, page.counts.borrow().clone()));
        }
        {
            let weak = Rc::downgrade(&page);
            ctx.on_packages_changed(move || {
                if let Some(page) = weak.upgrade() {
                    page.reload();
                }
            });
        }
        page.reload();
        page
    }

    /// Called with the total whenever it changes (the sidebar's badge).
    pub fn connect_count(&self, f: impl Fn(usize) + 'static) {
        *self.on_count.borrow_mut() = Some(Box::new(f));
    }

    pub fn reload(self: &Rc<Self>) {
        self.stack.set_visible_child_name("loading");
        self.update_all.set_sensitive(false);
        let backends = self.ctx.backends.clone();
        let page = self.clone();
        util::background(move || backends.iter().map(|b| b.list_updates()).collect::<Vec<Vec<PackageInfo>>>(), move |per_source| page.show(per_source));
    }

    fn show(self: &Rc<Self>, per_source: Vec<Vec<PackageInfo>>) {
        for group in self.groups.borrow_mut().drain(..) {
            self.content.remove(&group);
        }
        let counts: Vec<(usize, usize)> = per_source.iter().enumerate().map(|(s, u)| (s, u.len())).collect();
        let total: usize = counts.iter().map(|(_, n)| n).sum();
        for (source, updates) in per_source.into_iter().enumerate().filter(|(_, u)| !u.is_empty()) {
            let group = adw::PreferencesGroup::builder()
                .title(self.ctx.source_name(source))
                .description(format!("{} update{}", updates.len(), if updates.len() == 1 { "" } else { "s" }))
                .build();
            let update_source = gtk::Button::builder().label("Update All").valign(gtk::Align::Center).build();
            {
                let ctx = self.ctx.clone();
                // No names: the source's own full update (pacman -Syu…).
                update_source.connect_clicked(move |_| actions::transaction(&ctx, source, Kind::Update, Vec::new()));
            }
            group.set_header_suffix(Some(&update_source));
            for update in updates {
                let row = adw::ActionRow::builder().title(glib_escape(&update.name)).subtitle(glib_escape(&version_change(&update))).build();
                row.add_prefix(&gtk::Image::builder().icon_name(util::package_icon(&update.name)).pixel_size(32).build());
                let button = gtk::Button::builder().icon_name("software-update-available-symbolic").tooltip_text("Update").valign(gtk::Align::Center).build();
                button.add_css_class("flat");
                let (ctx, name) = (self.ctx.clone(), update.name.clone());
                button.connect_clicked(move |_| actions::transaction(&ctx, source, Kind::Update, vec![name.clone()]));
                row.add_suffix(&button);
                group.add(&row);
            }
            self.content.add(&group);
            self.groups.borrow_mut().push(group);
        }
        *self.counts.borrow_mut() = counts;
        self.update_all.set_sensitive(total > 0);
        self.summary.set_title(&format!("{total} update{} available", if total == 1 { "" } else { "s" }));
        self.summary.set_subtitle(
            &self.counts.borrow().iter().filter(|(_, n)| *n > 0).map(|(s, n)| format!("{} {n}", self.ctx.source_name(*s))).collect::<Vec<_>>().join(" · "),
        );
        self.title.set_subtitle(&if total > 0 { format!("{total} pending") } else { String::new() });
        self.stack.set_visible_child_name(if total > 0 { "list" } else { "none" });
        if let Some(f) = self.on_count.borrow().as_ref() {
            f(total);
        }
    }
}

fn version_change(update: &PackageInfo) -> String {
    match (update.installed_version.is_empty(), update.available_version.is_empty()) {
        (false, false) => format!("{} → {}", update.installed_version, update.available_version),
        (true, false) => format!("Update to {}", update.available_version),
        _ => update.description.clone(),
    }
}

/// Row titles are markup; package names and versions aren't.
fn glib_escape(text: &str) -> String {
    gtk::glib::markup_escape_text(text).to_string()
}
