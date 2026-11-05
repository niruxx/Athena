//! A long list of packages from every source (thousands, for Installed):
//! icon, name, summary, version, source, and its actions.

use std::cell::{Cell, RefCell};
use std::rc::Rc;

use adw::prelude::*;
use gtk::{gio, glib};

use crate::actions::{self, Kind};
use crate::context::{Context, Item};
use crate::util;

/// A package group filter: (source index, package names in the group).
type GroupMembers = (usize, std::collections::HashSet<String>);

pub struct PackageList {
    pub widget: gtk::ScrolledWindow,
    store: gio::ListStore,
    filter: gtk::CustomFilter,
    filtered: gtk::FilterListModel,
    text: Rc<RefCell<String>>,
    source: Rc<Cell<Option<usize>>>,
    apps_only: Rc<Cell<bool>>,
    repository: Rc<RefCell<String>>,
    architecture: Rc<RefCell<String>>,
    members: Rc<RefCell<Option<GroupMembers>>>,
    _settings: Option<gio::Settings>,
}

fn item_of(list_item: &gtk::ListItem) -> Option<Item> {
    list_item.item().and_downcast::<glib::BoxedAnyObject>().map(|o| o.borrow::<Item>().clone())
}

fn row_widgets(ctx: &Rc<Context>, list_item: &gtk::ListItem, selection: &gtk::MultiSelection, selecting: &gtk::ToggleButton) -> gtk::Box {
    let icon = gtk::Image::builder().pixel_size(32).build();
    let title = util::label("", &["heading"]);
    let subtitle = util::label("", &["dim-label", "caption"]);
    let text = gtk::Box::builder()
        .orientation(gtk::Orientation::Vertical)
        .spacing(2)
        .hexpand(true)
        .valign(gtk::Align::Center)
        .build();
    text.append(&title);
    text.append(&subtitle);
    let version = gtk::Label::builder()
        .xalign(1.0)
        .valign(gtk::Align::Center)
        .max_width_chars(24)
        .ellipsize(gtk::pango::EllipsizeMode::Middle)
        .build();
    version.add_css_class("dim-label");
    version.add_css_class("numeric");
    version.add_css_class("caption");
    let source = gtk::Label::builder().valign(gtk::Align::Center).build();
    source.add_css_class("source-pill");
    source.add_css_class("caption");

    // Not installed: Install. Installed: a menu with Reinstall and Remove.
    let install = gtk::Button::builder().label(crate::i18n::gettext("Install")).valign(gtk::Align::Center).build();
    install.add_css_class("suggested-action");
    let menu = gtk::MenuButton::builder()
        .icon_name("view-more-symbolic")
        .valign(gtk::Align::Center)
        .tooltip_text(crate::i18n::gettext("More"))
        .build();
    menu.add_css_class("flat");
    let reinstall = gtk::Button::builder().label(crate::i18n::gettext("Reinstall")).build();
    let remove = gtk::Button::builder().label(crate::i18n::gettext("Remove…")).build();
    reinstall.add_css_class("flat");
    remove.add_css_class("flat");
    remove.add_css_class("destructive-action");
    let popover_box = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(6).margin_top(8).margin_bottom(8).margin_start(8).margin_end(8).build();
    let details = gtk::Button::with_label(&crate::i18n::gettext("Details"));
    let download = gtk::Button::with_label(&crate::i18n::gettext("Download…"));
    popover_box.append(&details);
    popover_box.append(&download);
    popover_box.append(&reinstall);
    popover_box.append(&remove);
    let update = gtk::Button::with_label(&crate::i18n::gettext("Update"));
    popover_box.append(&update);
    let from = gtk::Button::with_label(&crate::i18n::gettext("Install from…"));
    popover_box.append(&from);
    let select = gtk::Button::with_label(&crate::i18n::gettext("Select"));
    popover_box.append(&select);
    {
        let (selection, selecting, weak) = (selection.clone(), selecting.clone(), list_item.downgrade());
        select.connect_clicked(move |_| {
            if let Some(item) = weak.upgrade() {
                let position = item.position();
                selecting.set_active(true);
                selection.select_item(position, false);
            }
        });
    }
    let popover = gtk::Popover::builder().child(&popover_box).build();
    menu.set_popover(Some(&popover));

    {
        let (ctx, weak, popover) = (ctx.clone(), list_item.downgrade(), popover.clone());
        let (ctx2, weak2, popover2) = (ctx.clone(), weak.clone(), popover.clone());
        from.connect_clicked(move |_| {
            popover2.popdown();
            if let Some(item) = weak2.upgrade().as_ref().and_then(item_of) {
                crate::details::show(&ctx2, item);
            }
        });
        details.connect_clicked(move |_| {
            popover.popdown();
            if let Some(item) = weak.upgrade().as_ref().and_then(item_of) {
                crate::details::show(&ctx, item);
            }
        });
    }
    {
        let (ctx, weak, popover) = (ctx.clone(), list_item.downgrade(), popover.clone());
        download.connect_clicked(move |_| {
            popover.popdown();
            if let Some(item) = weak.upgrade().as_ref().and_then(item_of) {
                actions::download(&ctx, item.source, vec![item.package.name]);
            }
        });
    }
    let weak = list_item.downgrade();
    let act = {
        let ctx = ctx.clone();
        let popover = popover.clone();
        move |kind: Kind| {
            popover.popdown();
            if let Some(item) = weak.upgrade().as_ref().and_then(item_of) {
                actions::transaction_item(&ctx, item, kind);
            }
        }
    };
    let a = act.clone();
    install.connect_clicked(move |_| a(Kind::Install));
    let a = act.clone();
    reinstall.connect_clicked(move |_| a(Kind::Reinstall));
    let a = act.clone();
    update.connect_clicked(move |_| a(Kind::Update));
    remove.connect_clicked(move |_| act(Kind::Remove));

    let row = gtk::Box::builder()
        .spacing(12)
        .margin_top(8)
        .margin_bottom(8)
        .margin_start(12)
        .margin_end(8)
        .build();
    for widget in [
        icon.upcast_ref::<gtk::Widget>(),
        text.upcast_ref(),
        version.upcast_ref(),
        source.upcast_ref(),
        install.upcast_ref(),
        menu.upcast_ref(),
    ] {
        row.append(widget);
    }
    let click = gtk::GestureClick::new();
    click.set_button(3);
    click.connect_pressed(move |_, _, _, _| popover.popup());
    row.add_controller(click);
    row
}

impl PackageList {
    pub fn new(ctx: &Rc<Context>) -> Self {
        let store = gio::ListStore::new::<glib::BoxedAnyObject>();
        let text: Rc<RefCell<String>> = Rc::default();
        let source: Rc<Cell<Option<usize>>> = Rc::default();
        let apps_only = Rc::new(Cell::new(false));
        let repository: Rc<RefCell<String>> = Rc::default();
        let architecture: Rc<RefCell<String>> = Rc::default();
        let members: Rc<RefCell<Option<GroupMembers>>> = Rc::default();
        let filter = {
            let (text, source, apps_only, repository, architecture, members) =
                (text.clone(), source.clone(), apps_only.clone(), repository.clone(), architecture.clone(), members.clone());
            gtk::CustomFilter::new(move |obj| {
                let item = obj.downcast_ref::<glib::BoxedAnyObject>().unwrap().borrow::<Item>();
                let needle = text.borrow();
                members.borrow().as_ref().is_none_or(|(s,names)| *s == item.source && names.contains(&item.package.name))
                    && (repository.borrow().is_empty() || item.package.repository.to_lowercase().contains(repository.borrow().as_str()))
                    && (architecture.borrow().is_empty() || item.package.architecture.to_lowercase() == *architecture.borrow())
                    && (!apps_only.get()
                        || crate::desktop::find(&item.package.name).is_some()
                        || athena_core::catalog::current().find(&item.package.name).is_some())
                    && source.get().is_none_or(|s| s == item.source)
                    && (needle.is_empty()
                        || item.package.name.to_lowercase().contains(needle.as_str())
                        || item.package.description.to_lowercase().contains(needle.as_str())
                        || athena_core::catalog::current()
                            .find(&item.package.name)
                            .is_some_and(|app| app.name.to_lowercase().contains(needle.as_str()) || app.summary.to_lowercase().contains(needle.as_str()))
                        || crate::desktop::find(&item.package.name).is_some_and(|app| app.display_name().to_lowercase().contains(needle.as_str())))
            })
        };
        let filtered = gtk::FilterListModel::new(Some(store.clone()), Some(filter.clone()));

        let selection = gtk::MultiSelection::new(Some(filtered.clone()));
        let selecting = gtk::ToggleButton::with_label("Select Packages");
        let factory = gtk::SignalListItemFactory::new();
        {
            let ctx = ctx.clone();
            let (selection, selecting) = (selection.clone(), selecting.clone());
            factory.connect_setup(move |_, obj| {
                let list_item = obj.downcast_ref::<gtk::ListItem>().unwrap();
                list_item.set_activatable(true);
                list_item.set_child(Some(&row_widgets(&ctx, list_item, &selection, &selecting)));
            });
        }
        {
            let ctx = ctx.clone();
            factory.connect_bind(move |_, obj| {
                let list_item = obj.downcast_ref::<gtk::ListItem>().unwrap();
                let (Some(item), Some(row)) = (item_of(list_item), list_item.child().and_downcast::<gtk::Box>()) else {
                    return;
                };
                let children: Vec<gtk::Widget> = std::iter::successors(row.first_child(), |w| w.next_sibling()).collect();
                let pkg = &item.package;
                children[0]
                    .downcast_ref::<gtk::Image>()
                    .unwrap()
                    .set_icon_name(Some(&util::package_icon(&pkg.name)));
                let text = children[1].downcast_ref::<gtk::Box>().unwrap();
                text.first_child().and_downcast::<gtk::Label>().unwrap().set_label(&pkg.name);
                let summary = text.last_child().and_downcast::<gtk::Label>().unwrap();
                let catalog = athena_core::catalog::current();
                let app = catalog.find(&pkg.name);
                if let Some(app) = app {
                    text.first_child().and_downcast::<gtk::Label>().unwrap().set_label(&app.name);
                    crate::catalog_ui::set_icon(children[0].downcast_ref::<gtk::Image>().unwrap(), &app.icon, &pkg.name);
                }
                let mut summary_text = app
                    .map(|a| a.summary.clone())
                    .filter(|s| !s.is_empty())
                    .unwrap_or_else(|| pkg.description.clone());
                if crate::settings::boolean("show-architecture") && !pkg.architecture.is_empty() {
                    summary_text.push_str(&format!(" · {}", pkg.architecture));
                }
                if crate::settings::boolean("show-size") && !pkg.size.is_empty() {
                    summary_text.push_str(&format!(" · {}", pkg.size));
                }
                summary.set_label(&summary_text);
                let margin = if crate::settings::boolean("compact-rows") { 2 } else { 8 };
                row.set_margin_top(margin);
                row.set_margin_bottom(margin);
                if let Some(app) = crate::desktop::find(&pkg.name) {
                    text.first_child().and_downcast::<gtk::Label>().unwrap().set_label(&app.display_name());
                    if let Some(icon) = app.icon() {
                        children[0].downcast_ref::<gtk::Image>().unwrap().set_from_gicon(&icon);
                    }
                }
                summary.set_visible(!summary_text.is_empty());
                let version = if pkg.installed { &pkg.installed_version } else { &pkg.available_version };
                children[2].downcast_ref::<gtk::Label>().unwrap().set_label(version);
                children[3].downcast_ref::<gtk::Label>().unwrap().set_label(ctx.source_name(item.source));
                children[4].set_visible(!pkg.installed);
                children[5].set_visible(true);
                let menu = children[5].downcast_ref::<gtk::MenuButton>().unwrap();
                if let Some(popover) = menu.popover()
                    && let Some(content) = popover.child()
                {
                    let buttons: Vec<_> = std::iter::successors(content.first_child(), |w| w.next_sibling()).collect();
                    buttons[2].set_visible(pkg.installed);
                    buttons[3].set_visible(pkg.installed);
                    buttons[4].set_visible(pkg.installed);
                }
            });
        }

        let view = gtk::ListView::builder().model(&selection).factory(&factory).show_separators(true).build();
        {
            let (ctx, model) = (ctx.clone(), filtered.clone());
            view.connect_activate(move |_, position| {
                if let Some(object) = model.item(position).and_downcast::<glib::BoxedAnyObject>() {
                    crate::details::show(&ctx, object.borrow::<Item>().clone());
                }
            });
        }
        let no_selection = gtk::NoSelection::new(Some(filtered.clone()));
        view.set_model(Some(&no_selection));
        let content = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(8).build();
        let batch = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(6).build();
        batch.set_visible(false);
        {
            let (view, batch, selection) = (view.clone(), batch.clone(), selection.clone());
            selecting.connect_toggled(move |button| {
                batch.set_visible(button.is_active());
                if button.is_active() {
                    view.set_model(Some(&selection));
                } else {
                    selection.unselect_all();
                    view.set_model(Some(&no_selection));
                }
            });
        }
        content.append(&selecting);
        let all = gtk::Button::with_label(&crate::i18n::gettext("Select all"));
        let none = gtk::Button::with_label(&crate::i18n::gettext("Clear selection"));
        let model = selection.clone();
        all.connect_clicked(move |_| {
            model.select_all();
        });
        let model = selection.clone();
        none.connect_clicked(move |_| {
            model.unselect_all();
        });
        batch.append(&all);
        batch.append(&none);
        for (label, kind) in [
            ("Install", Some(Kind::Install)),
            ("Remove", Some(Kind::Remove)),
            ("Update", Some(Kind::Update)),
            ("Download", None),
        ] {
            let button = gtk::Button::with_label(label);
            let (ctx, model, filtered) = (ctx.clone(), selection.clone(), filtered.clone());
            button.connect_clicked(move |_| {
                let mut groups = std::collections::BTreeMap::<usize, Vec<String>>::new();
                for index in 0..filtered.n_items() {
                    if model.is_selected(index)
                        && let Some(object) = filtered.item(index).and_downcast::<glib::BoxedAnyObject>()
                    {
                        let item = object.borrow::<Item>();
                        let eligible = match kind {
                            Some(Kind::Install) => !item.package.installed,
                            Some(Kind::Remove | Kind::Update | Kind::Reinstall) => item.package.installed,
                            None => true,
                        };
                        if eligible {
                            groups.entry(item.source).or_default().push(item.package.name.clone());
                        }
                    }
                }
                if groups.is_empty() {
                    ctx.toast(&crate::i18n::gettext("No selected packages support this action."));
                    return;
                }
                let groups = groups.into_iter().collect();
                if let Some(kind) = kind {
                    actions::batch(&ctx, kind, groups);
                } else {
                    actions::download_batch(&ctx, groups);
                }
            });
            batch.append(&button);
        }
        content.append(&batch);
        content.append(&view);
        view.add_css_class("package-list");
        view.add_css_class("card");
        let clamp = adw::Clamp::builder()
            .maximum_size(1000)
            .child(&content)
            .margin_top(12)
            .margin_bottom(24)
            .margin_start(12)
            .margin_end(12)
            .build();
        let widget = gtk::ScrolledWindow::builder()
            .child(&clamp)
            .vexpand(true)
            .hscrollbar_policy(gtk::PolicyType::Never)
            .build();
        let settings = crate::settings::load();
        if let Some(settings) = &settings {
            let weak = store.downgrade();
            settings.connect_changed(None, move |_, key| {
                if ["show-architecture", "show-size", "compact-rows"].contains(&key)
                    && let Some(store) = weak.upgrade()
                {
                    store.items_changed(0, store.n_items(), store.n_items());
                }
            });
        }
        {
            let weak = store.downgrade();
            let filter = filter.clone();
            ctx.on_catalog_changed(move || {
                if let Some(store) = weak.upgrade() {
                    filter.changed(gtk::FilterChange::Different);
                    store.items_changed(0, store.n_items(), store.n_items());
                }
            });
        }
        Self {
            widget,
            store,
            filter,
            filtered,
            text,
            source,
            apps_only,
            repository,
            architecture,
            members,
            _settings: settings,
        }
    }

    pub fn set_items(&self, items: Vec<Item>) {
        let objects: Vec<glib::BoxedAnyObject> = items.into_iter().map(glib::BoxedAnyObject::new).collect();
        self.store.splice(0, self.store.n_items(), &objects);
    }

    pub fn set_text(&self, text: &str) {
        *self.text.borrow_mut() = text.trim().to_lowercase();
        self.filter.changed(gtk::FilterChange::Different);
    }

    pub fn set_apps_only(&self, active: bool) {
        self.apps_only.set(active);
        self.filter.changed(gtk::FilterChange::Different);
    }

    pub fn set_repository(&self, value: &str) {
        *self.repository.borrow_mut() = value.to_lowercase();
        self.filter.changed(gtk::FilterChange::Different);
    }
    pub fn set_architecture(&self, value: &str) {
        *self.architecture.borrow_mut() = value.to_lowercase();
        self.filter.changed(gtk::FilterChange::Different);
    }

    pub fn set_group(&self, group: Option<GroupMembers>) {
        *self.members.borrow_mut()=group;self.filter.changed(gtk::FilterChange::Different);
    }

    pub fn set_source(&self, source: Option<usize>) {
        self.source.set(source);
        self.filter.changed(gtk::FilterChange::Different);
    }

    /// How many items pass the filters.
    pub fn visible_count(&self) -> u32 {
        self.filtered.n_items()
    }

    pub fn hidden_apps_count(&self) -> u32 {
        (0..self.store.n_items())
            .filter(|i| {
                self.store.item(*i).and_downcast::<glib::BoxedAnyObject>().is_some_and(|o| {
                    crate::desktop::find(&o.borrow::<Item>().package.name).is_none()
                        && athena_core::catalog::current().find(&o.borrow::<Item>().package.name).is_none()
                })
            })
            .count() as u32
    }

    pub fn total_count(&self) -> u32 {
        self.store.n_items()
    }
}

