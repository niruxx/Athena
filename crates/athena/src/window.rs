//! The main window: a sidebar of pages beside the current page, collapsing
//! to one pane in narrow windows.

use adw::prelude::*;
use gtk::gio;

use crate::context::Context;
use crate::pages::{installed::InstalledPage, repositories::RepositoriesPage, search::SearchPage, updates::UpdatesPage};

struct Destination {
    key: &'static str,
    title: &'static str,
    icon: &'static str,
}

const DESTINATIONS: &[Destination] = &[
    Destination { key: "search", title: "Search", icon: "system-search-symbolic" },
    Destination { key: "installed", title: "Installed", icon: "view-grid-symbolic" },
    Destination { key: "updates", title: "Updates", icon: "software-update-available-symbolic" },
    Destination { key: "repositories", title: "Repositories", icon: "network-server-symbolic" },
    Destination { key: "groups", title: "Groups", icon: "folder-symbolic" },
    Destination { key: "history", title: "History", icon: "document-open-recent-symbolic" },
];

fn sidebar_row(destination: &Destination) -> (gtk::ListBoxRow, gtk::Label) {
    let row_box = gtk::Box::builder().spacing(12).margin_top(4).margin_bottom(4).build();
    row_box.append(&gtk::Image::from_icon_name(destination.icon));
    row_box.append(&gtk::Label::builder().label(destination.title).xalign(0.0).hexpand(true).build());
    let badge = gtk::Label::builder().visible(false).build();
    badge.add_css_class("badge");
    badge.add_css_class("numeric");
    row_box.append(&badge);
    (gtk::ListBoxRow::builder().child(&row_box).build(), badge)
}

pub fn build(app: &adw::Application) -> adw::ApplicationWindow {
    let window = adw::ApplicationWindow::builder().application(app).title("Athena").default_width(1100).default_height(760).width_request(360).height_request(400).build();
    let toasts = adw::ToastOverlay::new();
    let mut backends = athena_core::distro::all_backends();
    if let Some(settings) = crate::settings::load() {
        for backend in &mut backends {
            if backend.key() == "dnf" { *backend = std::sync::Arc::new(athena_core::backends::DnfBackend::new().with_metadata_expire_hours(settings.uint("dnf-metadata-hours"))); }
        }
    }
    let ctx = Context::new(backends, window.clone(), toasts.clone());

    if ctx.backends.is_empty() {
        let status = adw::StatusPage::builder()
            .icon_name("dialog-warning-symbolic")
            .title("No Package Manager Found")
            .description("Athena works with pacman, DNF, APT, Flatpak, and Snap, and couldn't find any of them on this system.")
            .build();
        let view = adw::ToolbarView::new();
        view.add_top_bar(&adw::HeaderBar::new());
        view.set_content(Some(&status));
        window.set_content(Some(&view));
        return window;
    }

    let search = SearchPage::new(&ctx);
    let installed = InstalledPage::new(&ctx);
    let updates = UpdatesPage::new(&ctx);
    let repositories = RepositoriesPage::new(&ctx);

    let stack = gtk::Stack::builder().transition_type(gtk::StackTransitionType::Crossfade).build();
    stack.add_named(&search.widget, Some("search"));
    stack.add_named(&installed.widget, Some("installed"));
    stack.add_named(&updates.widget, Some("updates"));
    stack.add_named(&repositories.widget, Some("repositories"));

    stack.add_named(&crate::pages::groups::build(&ctx), Some("groups"));
    stack.add_named(&crate::pages::history::build(&ctx), Some("history"));

    // Sidebar.
    let list = gtk::ListBox::new();
    list.add_css_class("navigation-sidebar");
    let mut badges = Vec::new();
    for destination in DESTINATIONS {
        let (row, badge) = sidebar_row(destination);
        if destination.key == "groups" {
            if let Some(settings) = crate::settings::load() { settings.bind("hide-groups", &row, "visible").invert_boolean().build(); }
        }
        list.append(&row);
        badges.push(badge);
    }
    let menu = gio::Menu::new();
    menu.append(Some("_Reload Package Information"), Some("app.reload"));
    menu.append(Some("_Preferences"), Some("app.preferences"));
    menu.append(Some("_Keyboard Shortcuts"), Some("win.shortcuts"));
    menu.append(Some("_About Athena"), Some("app.about"));
    let menu_button = gtk::MenuButton::builder().icon_name("open-menu-symbolic").menu_model(&menu).primary(true).tooltip_text("Main Menu").build();
    let sidebar_header = adw::HeaderBar::new();
    sidebar_header.pack_end(&menu_button);
    let sidebar_view = adw::ToolbarView::new();
    sidebar_view.add_top_bar(&sidebar_header);
    sidebar_view.set_content(Some(&gtk::ScrolledWindow::builder().child(&list).hscrollbar_policy(gtk::PolicyType::Never).build()));
    let sidebar = adw::NavigationPage::builder().title("Athena").child(&sidebar_view).build();

    toasts.set_child(Some(&stack));
    let content = adw::NavigationPage::builder().title("Search").child(&toasts).build();

    let split = adw::NavigationSplitView::builder().sidebar(&sidebar).content(&content).min_sidebar_width(200.0).max_sidebar_width(260.0).build();
    {
        let (stack, content, split) = (stack.clone(), content.clone(), split.clone());
        list.connect_row_selected(move |_, row| {
            let Some(row) = row else { return };
            let destination = &DESTINATIONS[row.index() as usize];
            stack.set_visible_child_name(destination.key);
            content.set_title(destination.title);
            split.set_show_content(true);
        });
    }
    let start = crate::settings::load().map(|s| s.string("start-page").to_string()).unwrap_or_default();
    let index = DESTINATIONS.iter().position(|d| d.key == start).unwrap_or(0) as i32;
    list.select_row(list.row_at_index(index).as_ref());

    // The Updates row's badge.
    {
        let badge = badges[2].clone();
        updates.connect_count(move |count| {
            badge.set_label(&count.to_string());
            badge.set_visible(count > 0);
        });
    }

    // One pane below 600sp, as phones and tiled windows need.
    let breakpoint = adw::Breakpoint::new(adw::BreakpointCondition::parse("max-width: 600sp").unwrap());
    breakpoint.add_setter(&split, "collapsed", Some(&true.to_value()));
    window.add_breakpoint(breakpoint);
    let shell = gtk::Box::builder().orientation(gtk::Orientation::Vertical).build();
    let banner = adw::Banner::builder().revealed(false).button_label("View Release").build();
    shell.append(&banner); shell.append(&split); split.set_vexpand(true);
    if crate::settings::boolean("check-releases") {
        crate::util::background(athena_core::releases::check, move |release| {
            if let Some((tag, url)) = release {
                banner.set_title(&format!("Athena {tag} is available")); banner.set_revealed(true);
                banner.connect_button_clicked(move |banner| { gtk::UriLauncher::new(&url).launch(None::<&gtk::Window>, None::<&gio::Cancellable>, |_| {}); banner.set_revealed(false); });
            }
        });
    }
    ctx.navigation.add(&adw::NavigationPage::builder().title("Athena").child(&shell).build());
    window.set_content(Some(&ctx.navigation));

    // Ctrl+F: the current page's filter (Installed), else Search.
    let find = gio::SimpleAction::new("find", None);
    {
        let (list, stack, search, installed) = (list.clone(), stack.clone(), search.clone(), installed.clone());
        find.connect_activate(move |_, _| {
            if stack.visible_child_name().as_deref() == Some("installed") {
                installed.search.grab_focus();
                return;
            }
            list.select_row(list.row_at_index(0).as_ref());
            search.search.grab_focus();
        });
    }
    window.add_action(&find);
    app.set_accels_for_action("win.find", &["<Control>f"]);

    let reload = gio::SimpleAction::new("reload", None);
    {
        let ctx = ctx.clone();
        reload.connect_activate(move |_, _| ctx.packages_changed());
    }
    app.add_action(&reload);
    app.set_accels_for_action("app.reload", &["<Control>r"]);

    let about = gio::SimpleAction::new("about", None);
    {
        let window = window.clone();
        about.connect_activate(move |_, _| {
            let dialog = adw::AboutDialog::builder()
                .application_name("Athena")
                .application_icon(crate::APP_ID)
                .version(athena_core::VERSION)
                .developer_name("niruxxdaboi")
                .license_type(gtk::License::Gpl30)
                .website("https://github.com/niruxx/Athena")
                .issue_url("https://github.com/niruxx/Athena/issues")
                .comments("One place for every package on your system: pacman, DNF, APT, Flatpak, and Snap.")
                .build();
            dialog.present(Some(&window));
        });
    }
    app.add_action(&about);
    #[cfg(debug_assertions)]
    if let Some(dir) = std::env::var_os("ATHENA_SCREENSHOTS") {
        let select = |index: i32| {
            let list = list.clone();
            move || list.select_row(list.row_at_index(index).as_ref())
        };
        let typed = {
            let (list, search) = (list.clone(), search.clone());
            move || {
                list.select_row(list.row_at_index(0).as_ref());
                search.search.set_text("firefox");
            }
        };
        let narrow = {
            let window = window.clone();
            move || window.set_default_size(420, 760)
        };
        crate::screenshots::run(
            window.clone(),
            dir.into(),
            vec![
                ("search-empty", Box::new(select(0))),
                ("search", Box::new(typed)),
                ("installed", Box::new(select(1))),
                ("updates", Box::new(select(2))),
                ("repositories", Box::new(select(3))),
                ("confirm", {
                    let ctx = ctx.clone();
                    Box::new(move || {
                        crate::actions::transaction(&ctx, 0, crate::actions::Kind::Install, vec!["gimp".into()]);
                    })
                }),
                ("terminal", {
                    let window = window.clone();
                    Box::new(move || {
                        if let Some(dialog) = window.visible_dialog() {
                            dialog.force_close();
                        }
                        let demo = "printf ':: Starting full system upgrade...\\n( 1/4) checking keys in keyring\\n( 2/4) upgrading mesa\\n( 3/4) upgrading firefox\\n'; sleep 30";
                        crate::terminal::run(&window, "Update everything", vec![athena_core::Command::new("sh", ["-c", demo])], false, |_| {});
                    })
                }),
                ("narrow", Box::new(narrow)),
            ],
        );
    }

    let setup = gio::SimpleAction::new("setup", None);
    { let ctx = ctx.clone(); setup.connect_activate(move |_, _| crate::setup::show(&ctx)); }
    window.add_action(&setup);
    if crate::settings::load().is_some_and(|s| !s.boolean("setup-complete")) {
        let ctx = ctx.clone();
        gtk::glib::idle_add_local_once(move || crate::setup::show(&ctx));
    }
    let shortcuts = gio::SimpleAction::new("shortcuts", None);
    { let window = window.clone(); shortcuts.connect_activate(move |_, _| {
        let dialog = adw::Dialog::builder().title("Keyboard Shortcuts").content_width(460).build();
        let page = adw::PreferencesPage::new(); let group = adw::PreferencesGroup::new();
        for (action, key) in [("Find packages", "Ctrl+F"), ("Reload package information", "Ctrl+R"), ("Preferences", "Ctrl+,"), ("Keyboard shortcuts", "Ctrl+?"), ("Close window", "Ctrl+W"), ("Quit", "Ctrl+Q"), ("Select multiple packages", "Ctrl+click / Shift+click")] {
            group.add(&adw::ActionRow::builder().title(action).subtitle(key).build());
        }
        page.add(&group); dialog.set_child(Some(&page)); dialog.present(Some(&window));
    }); }
    window.add_action(&shortcuts);
    app.set_accels_for_action("win.shortcuts", &["<Control>question"]);
    app.set_accels_for_action("app.quit", &["<Control>q"]);
    app.set_accels_for_action("window.close", &["<Control>w"]);

    window
}
