//! Application-first browsing: a hero search, one scrolling category strip,
//! and roomy cards that show size, version, source and install state.
use crate::{context::Context, util};
use adw::prelude::*;
use athena_core::catalog::{self, App};
use gtk::{gio, glib};
use std::{
    cell::{Cell, RefCell},
    collections::HashMap,
    rc::Rc,
};

#[derive(Default, Clone)]
struct Indicator {
    installed: bool,
    size: String,
    version: String,
}
type Indicators = HashMap<(String, String), Indicator>;

pub struct DiscoverPage {
    pub widget: adw::ToolbarView,
    store: gio::ListStore,
    filter: gtk::CustomFilter,
    filtered: gtk::FilterListModel,
    text: Rc<RefCell<String>>,
    category: Rc<Cell<u32>>,
    source: Rc<RefCell<Option<String>>>,
    repository: Rc<RefCell<String>>,
    architecture: Rc<RefCell<String>>,
    installed_only: Rc<Cell<bool>>,
    stack: gtk::Stack,
    count: gtk::Label,
    heading: gtk::Label,
    grid: gtk::GridView,
    ctx: Rc<Context>,
    indicators: Rc<RefCell<Indicators>>,
    metadata_busy: Cell<bool>,
}

fn source_label(backend: &str) -> String {
    match backend {
        "flatpak" => "Flatpak".into(),
        "snap" => "Snap".into(),
        "aur" => "AUR".into(),
        "dnf" => "DNF".into(),
        "apt" => "APT".into(),
        other => other.into(),
    }
}

/// Card widgets, kept together so binding never walks the widget tree.
struct Card {
    root: gtk::Box,
    icon: gtk::Image,
    title: gtk::Label,
    sources: gtk::Label,
    status: gtk::Label,
    summary: gtk::Label,
    size: gtk::Label,
    version: gtk::Label,
}

fn pill(icon: &str) -> (gtk::Box, gtk::Label) {
    let row = gtk::Box::builder().spacing(5).build();
    row.add_css_class("app-indicator");
    row.append(&gtk::Image::builder().icon_name(icon).pixel_size(12).build());
    let label = gtk::Label::builder().ellipsize(gtk::pango::EllipsizeMode::End).max_width_chars(14).build();
    row.append(&label);
    (row, label)
}

fn card() -> Card {
    let root = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(14).width_request(280).build();
    root.add_css_class("card");
    root.add_css_class("app-tile");
    let top = gtk::Box::builder().spacing(14).build();
    let icon = gtk::Image::builder().pixel_size(64).valign(gtk::Align::Start).build();
    icon.add_css_class("app-icon");
    top.append(&icon);
    let names = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(4).hexpand(true).valign(gtk::Align::Center).build();
    let title = gtk::Label::builder()
        .xalign(0.)
        .wrap(true)
        .wrap_mode(gtk::pango::WrapMode::WordChar)
        .lines(2)
        .ellipsize(gtk::pango::EllipsizeMode::End)
        .build();
    title.add_css_class("app-title");
    names.append(&title);
    let sources = util::label("", &["caption", "dim-label"]);
    names.append(&sources);
    top.append(&names);
    let status = gtk::Label::builder().valign(gtk::Align::Start).build();
    status.add_css_class("app-indicator");
    top.append(&status);
    root.append(&top);
    let summary = gtk::Label::builder()
        .xalign(0.)
        .yalign(0.)
        .wrap(true)
        .wrap_mode(gtk::pango::WrapMode::WordChar)
        .lines(3)
        .ellipsize(gtk::pango::EllipsizeMode::End)
        .height_request(56)
        .vexpand(true)
        .build();
    summary.add_css_class("app-summary");
    summary.add_css_class("dim-label");
    root.append(&summary);
    let footer = gtk::Box::builder().spacing(8).build();
    footer.add_css_class("app-footer");
    let (size_pill, size) = pill("drive-harddisk-symbolic");
    let (version_pill, version) = pill("tag-symbolic");
    footer.append(&size_pill);
    footer.append(&version_pill);
    let spacer = gtk::Box::builder().hexpand(true).build();
    footer.append(&spacer);
    footer.append(&gtk::Image::from_icon_name("go-next-symbolic"));
    root.append(&footer);
    Card { root, icon, title, sources, status, summary, size, version }
}

impl DiscoverPage {
    pub fn new(ctx: &Rc<Context>) -> Rc<Self> {
        let widget = adw::ToolbarView::new();
        let header = adw::HeaderBar::builder().title_widget(&adw::WindowTitle::new("Discover", "")).build();
        let refresh = gtk::Button::builder().icon_name("view-refresh-symbolic").tooltip_text("Refresh application catalogs").build();
        header.pack_end(&refresh);
        widget.add_top_bar(&header);

        // Hero: a big search box with the source picker and advanced filters beside it.
        let hero = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(14).build();
        hero.add_css_class("discover-hero");
        let search = gtk::SearchEntry::builder().placeholder_text("Search applications").hexpand(true).build();
        search.set_search_delay(300);
        let filters = gtk::MenuButton::builder()
            .icon_name(util::themed_icon(&["view-filter-symbolic", "preferences-system-symbolic"]))
            .tooltip_text("Advanced filters")
            .valign(gtk::Align::Center)
            .build();
        hero.append(&search);
        let input = gtk::Box::builder().spacing(8).halign(gtk::Align::End).hexpand(true).build();
        let installed = gtk::ToggleButton::builder().label("Installed").valign(gtk::Align::Center).build();
        installed.add_css_class("filter-chip");
        input.append(&installed);

        let categories = gtk::DropDown::from_strings(&catalog::CATEGORIES.iter().map(|(_, label)| *label).collect::<Vec<_>>());
        let fields = gtk::Box::builder()
            .orientation(gtk::Orientation::Vertical)
            .spacing(12)
            .margin_top(18)
            .margin_bottom(18)
            .margin_start(18)
            .margin_end(18)
            .width_request(260)
            .build();
        fields.append(&util::label("Advanced filters", &["heading"]));
        let repository = gtk::Entry::builder().placeholder_text("Repository (all by default)").build();
        let architecture = gtk::Entry::builder().placeholder_text("Architecture (all by default)").build();
        fields.append(&repository);
        fields.append(&architecture);
        filters.set_popover(Some(&gtk::Popover::builder().child(&fields).build()));

        // Every category on one line; it scrolls sideways instead of wrapping into a tall toolbar.
        let strip = gtk::Box::builder().spacing(8).build();
        strip.add_css_class("category-strip");
        let mut quick_buttons: Vec<(u32, gtk::ToggleButton)> = vec![];
        let icons = [
            "view-app-grid-symbolic",
            "applications-multimedia-symbolic",
            "applications-engineering-symbolic",
            "applications-science-symbolic",
            "applications-games-symbolic",
            "applications-graphics-symbolic",
            "network-wireless-symbolic",
            "x-office-document-symbolic",
            "applications-science-symbolic",
            "applications-system-symbolic",
            "applications-utilities-symbolic",
        ];
        let first = gtk::ToggleButton::new();
        for (index, (_, label)) in catalog::CATEGORIES.iter().enumerate() {
            let button = if index == 0 { first.clone() } else { gtk::ToggleButton::new() };
            if index > 0 {
                button.set_group(Some(&first));
            }
            let content = adw::ButtonContent::builder().label(*label).icon_name(icons.get(index).copied().unwrap_or("view-app-grid-symbolic")).build();
            button.set_child(Some(&content));
            button.add_css_class("filter-chip");
            let picker = categories.clone();
            button.connect_toggled(move |button| {
                if button.is_active() {
                    picker.set_selected(index as u32);
                }
            });
            strip.append(&button);
            quick_buttons.push((index as u32, button));
        }
        first.set_active(true);
        let strip_scroll = gtk::ScrolledWindow::builder()
            .child(&strip)
            .vscrollbar_policy(gtk::PolicyType::Never)
            .hscrollbar_policy(gtk::PolicyType::Automatic)
            .hexpand(true)
            .build();
        hero.append(&strip_scroll);

        let store = gio::ListStore::new::<glib::BoxedAnyObject>();
        let text: Rc<RefCell<String>> = Rc::default();
        let category = Rc::new(Cell::new(0));
        let source: Rc<RefCell<Option<String>>> = Rc::default();
        let repo: Rc<RefCell<String>> = Rc::default();
        let arch: Rc<RefCell<String>> = Rc::default();
        let installed_only = Rc::new(Cell::new(false));
        let indicators: Rc<RefCell<Indicators>> = Rc::default();
        let filter = {
            let (text, category, source, repo, arch, installed_only, indicators) =
                (text.clone(), category.clone(), source.clone(), repo.clone(), arch.clone(), installed_only.clone(), indicators.clone());
            gtk::CustomFilter::new(move |object| {
                let app = object.downcast_ref::<glib::BoxedAnyObject>().unwrap().borrow::<App>();
                let needle = text.borrow();
                (needle.is_empty()
                    || app.name.to_lowercase().contains(needle.as_str())
                    || app.summary.to_lowercase().contains(needle.as_str())
                    || app.offers.iter().any(|o| o.package.to_lowercase().contains(needle.as_str())))
                    && (category.get() == 0 || app.categories.iter().any(|c| c == catalog::CATEGORIES[category.get() as usize].0))
                    && (!installed_only.get()
                        || app.offers.iter().any(|o| indicators.borrow().get(&(o.backend.clone(), o.package.clone())).is_some_and(|m| m.installed)))
                    && app.offers.iter().any(|o| {
                        source.borrow().as_ref().is_none_or(|s| s == &o.backend)
                            && (repo.borrow().is_empty() || o.repository.to_lowercase().contains(repo.borrow().as_str()))
                            && (arch.borrow().is_empty() || o.architecture == *arch.borrow())
                    })
            })
        };
        let filtered = gtk::FilterListModel::new(Some(store.clone()), Some(filter.clone()));

        let cards: Rc<RefCell<HashMap<gtk::Box, Card>>> = Rc::default();
        let factory = gtk::SignalListItemFactory::new();
        {
            let cards = cards.clone();
            factory.connect_setup(move |_, object| {
                let item = object.downcast_ref::<gtk::ListItem>().unwrap();
                let card = card();
                item.set_child(Some(&card.root));
                cards.borrow_mut().insert(card.root.clone(), card);
            });
        }
        {
            let cards = cards.clone();
            factory.connect_teardown(move |_, object| {
                if let Some(root) = object.downcast_ref::<gtk::ListItem>().and_then(|i| i.child()).and_downcast::<gtk::Box>() {
                    cards.borrow_mut().remove(&root);
                }
            });
        }
        {
            let (indicators, cards) = (indicators.clone(), cards.clone());
            factory.connect_bind(move |_, object| {
                let item = object.downcast_ref::<gtk::ListItem>().unwrap();
                let object = item.item().and_downcast::<glib::BoxedAnyObject>().unwrap();
                let app = object.borrow::<App>();
                let Some(root) = item.child().and_downcast::<gtk::Box>() else { return };
                let cards = cards.borrow();
                let Some(card) = cards.get(&root) else { return };
                crate::catalog_ui::set_icon(&card.icon, &app.icon, &app.id);
                let metadata = indicators.borrow();
                let known = || app.offers.iter().filter_map(|o| metadata.get(&(o.backend.clone(), o.package.clone())));
                let details = known().find(|m| m.installed).or_else(|| known().find(|m| !m.size.is_empty()));
                let installed = details.is_some_and(|m| m.installed);
                card.status.set_label(if installed { "Installed" } else { "Get" });
                if installed {
                    card.status.add_css_class("installed");
                    card.status.remove_css_class("accent");
                } else {
                    card.status.add_css_class("accent");
                    card.status.remove_css_class("installed");
                }
                card.title.set_label(&app.name);
                card.summary.set_label(if app.summary.is_empty() { "Open for application details and source choices." } else { &app.summary });
                let mut sources = app.offers.iter().map(|o| source_label(&o.backend)).collect::<Vec<String>>();
                sources.sort();
                sources.dedup();
                card.sources.set_label(&sources.join(" · "));
                let size = details.filter(|m| !m.size.is_empty()).map(|m| m.size.clone());
                card.size.set_label(size.as_deref().unwrap_or("Size unknown"));
                card.size.parent().unwrap().set_opacity(if size.is_some() { 1.0 } else { 0.6 });
                let version = details.filter(|m| !m.version.is_empty()).map(|m| m.version.clone());
                card.version.set_label(version.as_deref().unwrap_or("—"));
                card.version.parent().unwrap().set_visible(version.is_some());
                root.set_tooltip_text(Some(&format!(
                    "{}\n{}\n{}{}",
                    app.name,
                    app.summary,
                    size.as_deref().map(|s| format!("Size {s}")).unwrap_or_else(|| "Size unknown".into()),
                    version.map(|v| format!("\nVersion {v}")).unwrap_or_default()
                )));
            });
        }
        let grid = gtk::GridView::builder()
            .model(&gtk::NoSelection::new(Some(filtered.clone())))
            .factory(&factory)
            .min_columns(1)
            .max_columns(4)
            .single_click_activate(true)
            .build();
        grid.add_css_class("discover-grid");
        {
            let (ctx, model) = (ctx.clone(), filtered.clone());
            grid.connect_activate(move |_, position| {
                if let Some(object) = model.item(position).and_downcast::<glib::BoxedAnyObject>() {
                    crate::details::show_catalog_app(&ctx, &object.borrow::<App>());
                }
            });
        }

        let count = util::label("", &["dim-label", "numeric"]);
        // Wraps onto two lines in narrow windows instead of truncating the heading.
        let caption = gtk::FlowBox::builder().selection_mode(gtk::SelectionMode::None).min_children_per_line(1).max_children_per_line(2).column_spacing(12).row_spacing(8).margin_start(4).margin_end(4).build();
        let titles = gtk::Box::builder().orientation(gtk::Orientation::Vertical).hexpand(true).valign(gtk::Align::Center).width_request(140).build();
        let heading = util::label("All apps", &["title-3"]);
        titles.append(&heading);
        count.add_css_class("caption");
        titles.append(&count);
        caption.append(&titles);
        caption.append(&input);
        hero.append(&caption);
        let controls = gtk::Box::builder().margin_top(12).margin_bottom(6).margin_start(18).margin_end(18).build();
        controls.append(&hero);
        hero.set_hexpand(true);
        widget.add_top_bar(&adw::Clamp::builder().maximum_size(1280).tightening_threshold(900).child(&controls).build());
        grid.set_margin_start(10);
        grid.set_margin_end(10);
        grid.set_margin_bottom(24);
        let stack = util::animated_stack();
        stack.add_named(
            &gtk::ScrolledWindow::builder()
                .child(&adw::ClampScrollable::builder().maximum_size(1300).tightening_threshold(900).child(&grid).build())
                .hscrollbar_policy(gtk::PolicyType::Never)
                .vexpand(true)
                .build(),
            Some("apps"),
        );
        stack.add_named(
            &adw::StatusPage::builder()
                .icon_name("system-search-symbolic")
                .title("No matching applications")
                .description("Try another category or remove a filter. Package and capability searches are in Search.")
                .build(),
            Some("no-results"),
        );
        let empty = adw::StatusPage::builder()
            .paintable(&util::app_logo())
            .title("Add an application catalog")
            .description("Install your distribution's AppStream data or refresh Flatpak metadata to browse applications.")
            .build();
        let install = gtk::Button::with_label("Install catalog support…");
        install.add_css_class("pill");
        install.add_css_class("suggested-action");
        install.set_halign(gtk::Align::Center);
        let ctx2 = ctx.clone();
        install.connect_clicked(move |_| {
            if let Some(source) = ctx2.backends.iter().position(|b| matches!(b.key(), "pacman" | "apt" | "dnf")) {
                let package = match ctx2.backends[source].key() {
                    "pacman" => "archlinux-appstream-data",
                    "dnf" => "appstream-data",
                    _ => "appstream",
                };
                crate::actions::transaction(&ctx2, source, crate::actions::Kind::Install, vec![package.into()]);
            }
        });
        empty.set_child(Some(&install));
        stack.add_named(&empty, Some("empty"));
        widget.set_content(Some(&stack));

        let page = Rc::new(Self {
            widget,
            store,
            filter,
            filtered,
            text,
            category,
            source,
            repository: repo,
            architecture: arch,
            installed_only,
            stack,
            count,
            heading,
            grid,
            ctx: ctx.clone(),
            indicators,
            metadata_busy: Cell::new(false),
        });
        {
            let weak = Rc::downgrade(&page);
            let keys = ctx.backends.iter().map(|b| b.key().to_owned()).collect::<Vec<_>>();
            let sources = util::source_dropdown(ctx, move |selected| {
                if let Some(page) = weak.upgrade() {
                    *page.source.borrow_mut() = selected.map(|i| keys[i].clone());
                    page.refilter();
                }
            });
            input.prepend(&sources);
            input.append(&filters);
        }
        {
            let page = page.clone();
            search.connect_search_changed(move |entry| {
                *page.text.borrow_mut() = entry.text().to_lowercase();
                page.refilter();
            });
        }
        {
            let page = page.clone();
            installed.connect_toggled(move |button| {
                page.installed_only.set(button.is_active());
                page.refilter();
            });
        }
        {
            let page = page.clone();
            categories.connect_selected_notify(move |choice| {
                page.category.set(choice.selected());
                for (index, button) in &quick_buttons {
                    if *index == choice.selected() {
                        button.set_active(true);
                    }
                }
                page.heading.set_label(catalog::CATEGORIES.get(choice.selected() as usize).map(|(_, l)| *l).unwrap_or("All apps"));
                page.refilter();
            });
        }
        for (entry, target) in [(repository, page.repository.clone()), (architecture, page.architecture.clone())] {
            let page = page.clone();
            entry.connect_changed(move |entry| {
                *target.borrow_mut() = entry.text().to_lowercase();
                page.refilter();
            });
        }
        {
            let ctx = ctx.clone();
            refresh.connect_clicked(move |_| ctx.reload_catalog());
        }
        {
            let weak = Rc::downgrade(&page);
            ctx.on_catalog_changed(move || {
                if let Some(page) = weak.upgrade() {
                    page.reload();
                    page.load_metadata();
                }
            });
        }
        {
            let weak = Rc::downgrade(&page);
            ctx.on_packages_changed(move || {
                if let Some(page) = weak.upgrade() {
                    page.load_metadata();
                }
            });
        }
        page.reload();
        page.load_metadata();
        page
    }

    fn refilter(&self) {
        self.filter.changed(gtk::FilterChange::Different);
        self.update_count();
        util::animate(&self.grid, "page-enter");
    }

    fn update_count(&self) {
        let n = self.filtered.n_items();
        self.count.set_label(&format!("{n} application{}", if n == 1 { "" } else { "s" }));
        self.stack.set_visible_child_name(if self.store.n_items() == 0 {
            "empty"
        } else if n == 0 {
            "no-results"
        } else {
            "apps"
        });
    }

    fn reload(&self) {
        let mut apps = catalog::current().apps.clone();
        for app in &mut apps {
            app.offers.retain(|offer| self.ctx.backends.iter().any(|b| b.key() == offer.backend));
        }
        apps.retain(|app| !app.offers.is_empty());
        apps.sort_by_key(|a| a.name.to_lowercase());
        let objects = apps.into_iter().map(glib::BoxedAnyObject::new).collect::<Vec<_>>();
        self.store.splice(0, self.store.n_items(), &objects);
        self.update_count();
    }

    fn load_metadata(self: &Rc<Self>) {
        if self.metadata_busy.replace(true) {
            return;
        }
        let backends = self.ctx.backends.clone();
        let catalog = catalog::current();
        let page = Rc::downgrade(self);
        util::background(
            move || {
                let mut map = Indicators::new();
                for backend in backends {
                    for p in backend.list_installed() {
                        map.insert((backend.key().into(), p.name), Indicator { installed: true, size: p.size, version: p.installed_version });
                    }
                    if backend.key() == "flatpak" {
                        let out = athena_core::process::run(
                            "flatpak",
                            &["remote-ls", "--cached", "--app", "--columns=application,installed-size,version"],
                            athena_core::process::LONG,
                        );
                        for line in out.stdout.lines() {
                            let fields: Vec<_> = line.split('\t').collect();
                            if fields.len() < 2 {
                                continue;
                            }
                            let entry = map.entry(("flatpak".into(), fields[0].into())).or_default();
                            if entry.size.is_empty() {
                                entry.size = fields[1].into();
                            }
                            if entry.version.is_empty()
                                && let Some(version) = fields.get(2)
                            {
                                entry.version = (*version).into();
                            }
                        }
                    } else if matches!(backend.key(), "pacman" | "apt" | "dnf") {
                        let mut names = catalog
                            .apps
                            .iter()
                            .flat_map(|a| a.offers.iter())
                            .filter(|o| o.backend == backend.key())
                            .map(|o| o.package.clone())
                            .collect::<Vec<_>>();
                        names.sort();
                        names.dedup();
                        for chunk in names.chunks(400) {
                            for p in backend.package_details(chunk) {
                                let entry = map.entry((backend.key().into(), p.name)).or_default();
                                if entry.size.is_empty() {
                                    entry.size = p.size;
                                }
                                if entry.version.is_empty() {
                                    entry.version = p.available_version;
                                }
                            }
                        }
                    }
                }
                map
            },
            move |map| {
                if let Some(page) = page.upgrade() {
                    *page.indicators.borrow_mut() = map;
                    page.metadata_busy.set(false);
                    let n = page.store.n_items();
                    page.store.items_changed(0, n, n);
                    if page.installed_only.get() {
                        page.filter.changed(gtk::FilterChange::Different);
                        page.update_count();
                    }
                }
            },
        );
    }
}
