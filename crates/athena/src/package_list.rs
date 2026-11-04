//! A long list of packages from every source (thousands, for Installed):
//! icon, name, summary, version, source, and its actions.

use std::cell::{Cell, RefCell};
use std::rc::Rc;

use adw::prelude::*;
use gtk::{gio, glib};

use crate::actions::{self, Kind};
use crate::context::{Context, Item};
use crate::util;

pub struct PackageList {
    pub widget: gtk::ScrolledWindow,
    store: gio::ListStore,
    filter: gtk::CustomFilter,
    filtered: gtk::FilterListModel,
    text: Rc<RefCell<String>>,
    source: Rc<Cell<Option<usize>>>,
    apps_only: Rc<Cell<bool>>,
}

fn item_of(list_item: &gtk::ListItem) -> Option<Item> {
    list_item.item().and_downcast::<glib::BoxedAnyObject>().map(|o| o.borrow::<Item>().clone())
}

fn row_widgets(ctx: &Rc<Context>, list_item: &gtk::ListItem) -> gtk::Box {
    let icon = gtk::Image::builder().pixel_size(32).build();
    let title = util::label("", &["heading"]);
    let subtitle = util::label("", &["dim-label", "caption"]);
    let text = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(2).hexpand(true).valign(gtk::Align::Center).build();
    text.append(&title);
    text.append(&subtitle);
    let version = gtk::Label::builder().xalign(1.0).valign(gtk::Align::Center).max_width_chars(24).ellipsize(gtk::pango::EllipsizeMode::Middle).build();
    version.add_css_class("dim-label");
    version.add_css_class("numeric");
    version.add_css_class("caption");
    let source = gtk::Label::builder().valign(gtk::Align::Center).build();
    source.add_css_class("source-pill");
    source.add_css_class("caption");

    // Not installed: Install. Installed: a menu with Reinstall and Remove.
    let install = gtk::Button::builder().label("Install").valign(gtk::Align::Center).build();
    install.add_css_class("suggested-action");
    let menu = gtk::MenuButton::builder().icon_name("view-more-symbolic").valign(gtk::Align::Center).tooltip_text("More").build();
    menu.add_css_class("flat");
    let reinstall = gtk::Button::builder().label("Reinstall").build();
    let remove = gtk::Button::builder().label("Remove…").build();
    reinstall.add_css_class("flat");
    remove.add_css_class("flat");
    remove.add_css_class("destructive-action");
    let popover_box = gtk::Box::builder().orientation(gtk::Orientation::Vertical).build();
    let details = gtk::Button::with_label("Details");
    let download = gtk::Button::with_label("Download…");
    popover_box.append(&details);
    popover_box.append(&download);
    popover_box.append(&reinstall);
    popover_box.append(&remove);
    let popover = gtk::Popover::builder().child(&popover_box).build();
    menu.set_popover(Some(&popover));

    {
        let (ctx, weak, popover) = (ctx.clone(), list_item.downgrade(), popover.clone());
        details.connect_clicked(move |_| {
            popover.popdown();
            if let Some(item) = weak.upgrade().as_ref().and_then(item_of) { crate::details::show(&ctx, item); }
        });
    }
    {
        let (ctx, weak, popover) = (ctx.clone(), list_item.downgrade(), popover.clone());
        download.connect_clicked(move |_| {
            popover.popdown();
            if let Some(item) = weak.upgrade().as_ref().and_then(item_of) { actions::download(&ctx, item.source, vec![item.package.name]); }
        });
    }
    let weak = list_item.downgrade();
    let act = {
        let ctx = ctx.clone();
        let popover = popover.clone();
        move |kind: Kind| {
            popover.popdown();
            if let Some(item) = weak.upgrade().as_ref().and_then(item_of) {
                actions::transaction(&ctx, item.source, kind, vec![item.package.name]);
            }
        }
    };
    let a = act.clone();
    install.connect_clicked(move |_| a(Kind::Install));
    let a = act.clone();
    reinstall.connect_clicked(move |_| a(Kind::Reinstall));
    remove.connect_clicked(move |_| act(Kind::Remove));

    let row = gtk::Box::builder().spacing(12).margin_top(8).margin_bottom(8).margin_start(12).margin_end(8).build();
    for widget in [icon.upcast_ref::<gtk::Widget>(), text.upcast_ref(), version.upcast_ref(), source.upcast_ref(), install.upcast_ref(), menu.upcast_ref()] {
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
        let app_names: std::collections::HashSet<String> = gio::AppInfo::all().into_iter().flat_map(|app| {
            let mut names = vec![app.id().unwrap_or_default().trim_end_matches(".desktop").to_owned()];
            if let Some(name) = app.executable().file_name() { names.push(name.to_string_lossy().into_owned()); }
            names
        }).collect();
        let filter = {
            let (text, source, apps_only) = (text.clone(), source.clone(), apps_only.clone());
            gtk::CustomFilter::new(move |obj| {
                let item = obj.downcast_ref::<glib::BoxedAnyObject>().unwrap().borrow::<Item>();
                let needle = text.borrow();
                (!apps_only.get() || app_names.contains(&item.package.name))
                    && source.get().is_none_or(|s| s == item.source)
                    && (needle.is_empty()
                        || item.package.name.to_lowercase().contains(needle.as_str())
                        || item.package.description.to_lowercase().contains(needle.as_str()))
            })
        };
        let filtered = gtk::FilterListModel::new(Some(store.clone()), Some(filter.clone()));

        let factory = gtk::SignalListItemFactory::new();
        {
            let ctx = ctx.clone();
            factory.connect_setup(move |_, obj| {
                let list_item = obj.downcast_ref::<gtk::ListItem>().unwrap();
                list_item.set_activatable(true);
                list_item.set_child(Some(&row_widgets(&ctx, list_item)));
            });
        }
        {
            let ctx = ctx.clone();
            factory.connect_bind(move |_, obj| {
                let list_item = obj.downcast_ref::<gtk::ListItem>().unwrap();
                let (Some(item), Some(row)) = (item_of(list_item), list_item.child().and_downcast::<gtk::Box>()) else { return };
                let children: Vec<gtk::Widget> = std::iter::successors(row.first_child(), |w| w.next_sibling()).collect();
                let pkg = &item.package;
                children[0].downcast_ref::<gtk::Image>().unwrap().set_icon_name(Some(&util::package_icon(&pkg.name)));
                let text = children[1].downcast_ref::<gtk::Box>().unwrap();
                text.first_child().and_downcast::<gtk::Label>().unwrap().set_label(&pkg.name);
                let summary = text.last_child().and_downcast::<gtk::Label>().unwrap();
                let mut summary_text = pkg.description.clone();
                if crate::settings::boolean("show-architecture") && !pkg.architecture.is_empty() { summary_text.push_str(&format!(" · {}", pkg.architecture)); }
                if crate::settings::boolean("show-size") && !pkg.size.is_empty() { summary_text.push_str(&format!(" · {}", pkg.size)); }
                summary.set_label(&summary_text);
                let margin = if crate::settings::boolean("compact-rows") { 2 } else { 8 };
                row.set_margin_top(margin); row.set_margin_bottom(margin);
                if let Some(app) = crate::desktop::find(&pkg.name) {
                    text.first_child().and_downcast::<gtk::Label>().unwrap().set_label(&app.display_name());
                    if let Some(icon) = app.icon() { children[0].downcast_ref::<gtk::Image>().unwrap().set_from_gicon(&icon); }
                }
                summary.set_visible(!pkg.description.is_empty());
                let version = if pkg.installed { &pkg.installed_version } else { &pkg.available_version };
                children[2].downcast_ref::<gtk::Label>().unwrap().set_label(version);
                children[3].downcast_ref::<gtk::Label>().unwrap().set_label(ctx.source_name(item.source));
                children[4].set_visible(!pkg.installed);
                children[5].set_visible(true);
            });
        }

        let selection = gtk::MultiSelection::new(Some(filtered.clone()));
        let view = gtk::ListView::builder()
            .model(&selection)
            .factory(&factory)
            .show_separators(true)
            .build();
        {
            let (ctx, model) = (ctx.clone(), filtered.clone());
            view.connect_activate(move |_, position| {
                if let Some(object) = model.item(position).and_downcast::<glib::BoxedAnyObject>() { crate::details::show(&ctx, object.borrow::<Item>().clone()); }
            });
        }
        let content = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(8).build();
        let batch = gtk::Box::builder().spacing(6).build();
        let all = gtk::Button::with_label("Select all");
        let none = gtk::Button::with_label("Clear selection");
        let model = selection.clone(); all.connect_clicked(move |_| { model.select_all(); });
        let model = selection.clone(); none.connect_clicked(move |_| { model.unselect_all(); });
        batch.append(&all); batch.append(&none);
        for (label, kind) in [("Install", Some(Kind::Install)), ("Remove", Some(Kind::Remove)), ("Update", Some(Kind::Update)), ("Download", None)] {
            let button = gtk::Button::with_label(label);
            let (ctx, model, filtered) = (ctx.clone(), selection.clone(), filtered.clone());
            button.connect_clicked(move |_| {
                let mut groups = std::collections::BTreeMap::<usize, Vec<String>>::new();
                for index in 0..filtered.n_items() {
                    if model.is_selected(index) {
                        if let Some(object) = filtered.item(index).and_downcast::<glib::BoxedAnyObject>() {
                            let item = object.borrow::<Item>();
                            groups.entry(item.source).or_default().push(item.package.name.clone());
                        }
                    }
                }
                if groups.is_empty() { ctx.toast("Select packages first (Ctrl+click or Shift+click)."); }
                for (source, names) in groups {
                    if let Some(kind) = kind { actions::transaction(&ctx, source, kind, names); }
                    else { actions::download(&ctx, source, names); }
                }
            });
            batch.append(&button);
        }
        content.append(&batch);
        content.append(&view);
        view.add_css_class("package-list");
        view.add_css_class("card");
        let clamp = adw::Clamp::builder().maximum_size(1000).child(&content).margin_top(12).margin_bottom(24).margin_start(12).margin_end(12).build();
        let widget = gtk::ScrolledWindow::builder().child(&clamp).vexpand(true).hscrollbar_policy(gtk::PolicyType::Never).build();
        Self { widget, store, filter, filtered, text, source, apps_only }
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

    pub fn set_source(&self, source: Option<usize>) {
        self.source.set(source);
        self.filter.changed(gtk::FilterChange::Different);
    }

    /// How many items pass the filters.
    pub fn visible_count(&self) -> u32 {
        self.filtered.n_items()
    }

    pub fn total_count(&self) -> u32 {
        self.store.n_items()
    }
}

/// "All sources", then each source, for a filter dropdown.
pub fn source_dropdown(ctx: &Context) -> gtk::DropDown {
    let mut names = vec!["All sources"];
    names.extend((0..ctx.backends.len()).map(|i| ctx.source_name(i)));
    gtk::DropDown::from_strings(&names)
}

/// The dropdown's choice as a source index (None for "All sources").
pub fn chosen_source(dropdown: &gtk::DropDown) -> Option<usize> {
    dropdown.selected().checked_sub(1).map(|s| s as usize)
}
