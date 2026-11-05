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
    section: &'static str,
}

const DESTINATIONS: &[Destination] = &[
    Destination { key:"discover",title:crate::i18n::gettext_noop("Discover"),icon:"system-software-install-symbolic",section:crate::i18n::gettext_noop("Browse") },
    Destination { key:"search",title:crate::i18n::gettext_noop("Search"),icon:"system-search-symbolic",section:crate::i18n::gettext_noop("Browse") },
    Destination { key:"home",title:crate::i18n::gettext_noop("Home"),icon:"go-home-symbolic",section:crate::i18n::gettext_noop("Browse") },
    Destination { key:"installed",title:crate::i18n::gettext_noop("Installed"),icon:"view-grid-symbolic",section:crate::i18n::gettext_noop("Library") },
    Destination { key:"updates",title:crate::i18n::gettext_noop("Updates"),icon:"software-update-available-symbolic",section:crate::i18n::gettext_noop("Library") },
    Destination { key:"history",title:crate::i18n::gettext_noop("History"),icon:"document-open-recent-symbolic",section:crate::i18n::gettext_noop("Library") },
    Destination { key:"repositories",title:crate::i18n::gettext_noop("Repositories"),icon:"network-server-symbolic",section:crate::i18n::gettext_noop("Sources") },
    Destination { key:"flatpak",title:crate::i18n::gettext_noop("Flatpak"),icon:"application-x-executable-symbolic",section:crate::i18n::gettext_noop("Sources") },
    Destination { key:"snap",title:crate::i18n::gettext_noop("Snap"),icon:"application-x-addon-symbolic",section:crate::i18n::gettext_noop("Sources") },
    Destination { key:"arch",title:crate::i18n::gettext_noop("Mirrors & Build Tools"),icon:"applications-system-symbolic",section:crate::i18n::gettext_noop("Sources") },
    Destination { key:"tools",title:crate::i18n::gettext_noop("Tools"),icon:"applications-utilities-symbolic",section:crate::i18n::gettext_noop("Sources") },
    Destination { key:"news",title:crate::i18n::gettext_noop("News"),icon:"mail-unread-symbolic",section:crate::i18n::gettext_noop("News") },
];
fn destination_index(key:&str)->i32 {DESTINATIONS.iter().position(|d|d.key==key).unwrap_or(0) as i32}
fn select_destination(list:&gtk::ListBox,key:&str){if let Some(row)=list.row_at_index(destination_index(key))&&row.is_visible(){list.select_row(Some(&row));}}

fn sidebar_row(destination: &Destination, title: &str) -> (gtk::ListBoxRow, gtk::Label) {
    let row_box = gtk::Box::builder().spacing(12).margin_top(5).margin_bottom(5).margin_start(4).margin_end(2).build();
    row_box.append(&gtk::Image::from_icon_name(destination.icon));
    row_box.append(
        &gtk::Label::builder()
            .label(title)
            .xalign(0.0)
            .hexpand(true)
            .ellipsize(gtk::pango::EllipsizeMode::End)
            .build(),
    );
    let badge = gtk::Label::builder().visible(false).build();
    badge.add_css_class("badge");
    badge.add_css_class("numeric");
    row_box.append(&badge);
    (gtk::ListBoxRow::builder().child(&row_box).tooltip_text(title).build(), badge)
}

pub fn build(app: &adw::Application) -> adw::ApplicationWindow {
    let window = adw::ApplicationWindow::builder()
        .application(app)
        .title(crate::i18n::gettext("Athena"))
        .default_width(1100)
        .default_height(760)
        .width_request(360)
        .height_request(400)
        .build();
    let toasts = adw::ToastOverlay::new();
    let mut backends = athena_core::distro::all_backends();
    if let Some(settings) = crate::settings::load() {
        for backend in &mut backends {
            if backend.key() == "dnf" {
                *backend = std::sync::Arc::new(athena_core::backends::DnfBackend::new().with_metadata_expire_hours(settings.uint("dnf-metadata-hours")));
            }
        }
    }
    if crate::settings::load().is_some_and(|s| !s.boolean("aur-enabled")) {
        backends.retain(|b| b.key() != "aur");
    }
    backends.retain(|backend|crate::settings::source_enabled(backend.key()));
    let ctx = Context::new(backends, window.clone(), toasts.clone());

    if ctx.backends.is_empty() {
        let status = adw::StatusPage::builder()
            .icon_name("dialog-warning-symbolic")
            .title(crate::i18n::gettext("No Package Manager Found"))
            .description(crate::i18n::gettext(
                "Athena works with pacman, DNF, APT, Flatpak, and Snap, and couldn't find any of them on this system.",
            ))
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

    let stack = gtk::Stack::builder().transition_type(gtk::StackTransitionType::Crossfade).transition_duration(240).build();
    stack.add_named(&search.widget, Some("search"));
    stack.add_named(&installed.widget, Some("installed"));
    stack.add_named(&updates.widget, Some("updates"));
    stack.add_named(&repositories.widget, Some("repositories"));

    stack.add_named(&crate::pages::history::build(&ctx), Some("history"));

    if ctx.backends.iter().any(|b|b.key()=="flatpak"){stack.add_named(&crate::pages::flatpak::build(&ctx),Some("flatpak"));}
    if ctx.backends.iter().any(|b|b.key()=="snap"){stack.add_named(&crate::pages::snap::build(&ctx),Some("snap"));}

    stack.add_named(&crate::pages::discover::DiscoverPage::new(&ctx).widget, Some("discover"));
    let is_arch=athena_core::distro::detect_family()==athena_core::distro::DistroFamily::Arch;
    if is_arch||ctx.backends.iter().any(|b|b.key()=="dnf"){stack.add_named(&crate::pages::arch::build(&ctx),Some("arch"));}
    let news_feed=athena_core::arch::news_feed();
    if let Some(feed)=&news_feed{stack.add_named(&crate::pages::news::build(&ctx,feed.clone()),Some("news"));}
    stack.add_named(&crate::pages::home::build(&ctx), Some("home"));
    stack.add_named(&crate::pages::tools::build(&ctx), Some("tools"));
    {
        let weak = std::rc::Rc::downgrade(&ctx);
        ctx.on_packages_changed(move || {
            if let Some(ctx) = weak.upgrade() {
                ctx.reload_catalog();
            }
        });
    }
    ctx.reload_catalog();

    // Sidebar.
    let list = gtk::ListBox::new();
    list.add_css_class("navigation-sidebar");
    let mut badges = Vec::new();
    for destination in DESTINATIONS {
        let title = match (destination.key, &news_feed) { ("news", Some(feed)) => feed.name.to_owned(), _ => crate::i18n::gettext(destination.title) };
        let (row, badge) = sidebar_row(destination, &title);
        row.set_visible(match destination.key {
            // Source pages only exist when that source is installed and enabled.
            "flatpak"|"snap"=>ctx.backends.iter().any(|b|b.key()==destination.key),
            "news"=>news_feed.is_some(),"arch"=>is_arch||ctx.backends.iter().any(|b|b.key()=="dnf"),_=>true,
        });
        list.append(&row);
        badges.push(badge);
    }
    // Section headings, placed above the first visible row of each section.
    list.set_header_func(|row, _| {
        let index = row.index() as usize;
        let section = DESTINATIONS[index].section;
        let first_visible = row.is_visible() && !(0..index).rev().filter_map(|i| row.parent().and_downcast::<gtk::ListBox>().and_then(|l| l.row_at_index(i as i32))).any(|r| r.is_visible() && DESTINATIONS[r.index() as usize].section == section);
        if first_visible {
            let heading = gtk::Label::builder().label(crate::i18n::gettext(section).to_uppercase()).xalign(0.0).build();
            heading.add_css_class("sidebar-heading");
            row.set_header(Some(&heading));
        } else {
            row.set_header(None::<&gtk::Widget>);
        }
    });
    list.invalidate_headers();
    let menu = gio::Menu::new();
    menu.append(Some("_Reload Package Information"), Some("app.reload"));
    menu.append(Some("_Preferences"), Some("app.preferences"));
    menu.append(Some("_Keyboard Shortcuts"), Some("win.shortcuts"));
    menu.append(Some("_About Athena"), Some("app.about"));
    let menu_button = gtk::MenuButton::builder()
        .icon_name("open-menu-symbolic")
        .menu_model(&menu)
        .primary(true)
        .tooltip_text(crate::i18n::gettext("Main Menu"))
        .build();
    let sidebar_header = adw::HeaderBar::new();
    sidebar_header.pack_end(&menu_button);
    let sidebar_view = adw::ToolbarView::new();
    sidebar_view.add_top_bar(&sidebar_header);
    sidebar_view.set_content(Some(
        &gtk::ScrolledWindow::builder().child(&list).hscrollbar_policy(gtk::PolicyType::Never).build(),
    ));
    let sidebar = adw::NavigationPage::builder()
        .title(crate::i18n::gettext("Athena"))
        .child(&sidebar_view)
        .build();

    let content = adw::NavigationPage::builder().title(crate::i18n::gettext("Search")).child(&stack).build();

    let split = adw::NavigationSplitView::builder()
        .sidebar(&sidebar)
        .content(&content)
        .min_sidebar_width(200.0)
        .max_sidebar_width(260.0)
        .build();
    {
        let (stack, content, split) = (stack.clone(), content.clone(), split.clone());
        list.connect_row_selected(move |_, row| {
            let Some(row) = row else { return };
            let destination = &DESTINATIONS[row.index() as usize];
            stack.set_visible_child_name(destination.key);
            if let Some(page) = stack.visible_child() { crate::util::animate(&page, "page-enter"); }
            content.set_title(&row.tooltip_text().map(|t| t.to_string()).unwrap_or_else(|| crate::i18n::gettext(destination.title)));
            split.set_show_content(true);
        });
    }
    let start = crate::settings::load().map(|s| s.string("start-page").to_string()).unwrap_or_default();
    let start=if start=="groups"{"search"}else{start.as_str()};
    let row=list.row_at_index(destination_index(start)).filter(|row|row.is_visible()).or_else(||list.row_at_index(0));list.select_row(row.as_ref());

    let show_updates = gio::SimpleAction::new("show-updates", None);
    {
        let list = list.clone();
        show_updates.connect_activate(move |_, _| select_destination(&list,"updates"));
    }
    window.add_action(&show_updates);
    let update_all = gio::SimpleAction::new("update-all", None);
    {
        let ctx = ctx.clone();
        update_all.connect_activate(move |_, _| {
            let backends = ctx.backends.clone();
            let ctx = ctx.clone();
            crate::util::background(
                move || backends.iter().enumerate().map(|(s, b)| (s, b.list_updates().len())).collect(),
                move |counts| crate::actions::update_everything(&ctx, counts),
            );
        });
    }
    window.add_action(&update_all);
    // The Updates row's badge.
    {
        let badge = badges[destination_index("updates") as usize].clone();
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
    shell.append(&banner);
    shell.append(&split);
    split.set_vexpand(true);
    if crate::settings::boolean("check-releases") {
        crate::util::background(athena_core::releases::check, move |release| {
            if let Some((tag, url)) = release {
                banner.set_title(&format!("Athena {tag} is available"));
                banner.set_revealed(true);
                banner.connect_button_clicked(move |banner| {
                    gtk::UriLauncher::new(&url).launch(None::<&gtk::Window>, None::<&gio::Cancellable>, |_| {});
                    banner.set_revealed(false);
                });
            }
        });
    }
    ctx.navigation.add(
        &adw::NavigationPage::builder()
            .tag("main")
            .title(crate::i18n::gettext("Athena"))
            .child(&shell)
            .build(),
    );
    toasts.set_child(Some(&ctx.navigation));
    window.set_content(Some(&toasts));

    // Ctrl+F: the current page's filter (Installed), else Search.
    let find = gio::SimpleAction::new("find", None);
    {
        let (list, stack, search, installed) = (list.clone(), stack.clone(), search.clone(), installed.clone());
        find.connect_activate(move |_, _| {
            if stack.visible_child_name().as_deref() == Some("installed") {
                installed.search.grab_focus();
                return;
            }
            select_destination(&list,"search");
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
        let select = |key: &'static str| {
            let list = list.clone();
            move || select_destination(&list,key)
        };
        let typed = {
            let (list, search) = (list.clone(), search.clone());
            move || {
                select_destination(&list,"search");
                search.search.set_text("firefox");
            }
        };
        let narrow = {
            let window = window.clone();
            move || window.set_default_size(420, 760)
        };
        crate::screenshots::run(
            window.clone(),
            ctx.navigation.clone(),
            dir.into(),
            vec![
                ("search-empty", Box::new(select("search"))),
                ("search", Box::new(typed)),
                ("installed", Box::new(select("installed"))),
                ("updates", Box::new(select("updates"))),
                ("repositories", Box::new(select("repositories"))),
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
                ("discover", Box::new(select("discover"))),
                ("flatpak", Box::new(select("flatpak"))),
                ("snap", Box::new(select("snap"))),
                ("arch", Box::new(select("arch"))),
                ("home", Box::new(select("home"))),
                ("tools", Box::new(select("tools"))),
                ("import-picker", {
                    let ctx = ctx.clone();
                    Box::new(move || crate::pages::tools::screenshot_import(&ctx))
                }),
                ("cleanup", {
                    let ctx = ctx.clone();
                    Box::new(move || crate::pages::tools::show_cleanup(&ctx))
                }),
                ("backup", {
                    let ctx = ctx.clone();
                    Box::new(move || crate::pages::tools::show_backup(&ctx))
                }),
                ("transfer", {
                    let ctx = ctx.clone();
                    Box::new(move || crate::pages::tools::show_transfer(&ctx))
                }),
                ("snapshots", {
                    let ctx = ctx.clone();
                    Box::new(move || crate::pages::tools::show_snapshots(&ctx))
                }),
                ("narrow-tools", {
                    let (window, list) = (window.clone(), list.clone());
                    Box::new(move || {
                        window.set_default_size(420, 760);
                        select_destination(&list,"tools");
                    })
                }),
                ("narrow-backup", {
                    let (window, ctx) = (window.clone(), ctx.clone());
                    Box::new(move || {
                        window.set_default_size(420, 760);
                        crate::pages::tools::show_backup(&ctx);
                    })
                }),
                ("aur-review", {
                    let ctx = ctx.clone();
                    Box::new(move || crate::aur_review::screenshot(&ctx))
                }),
                ("groups", {let(list,search)=(list.clone(),search.clone());Box::new(move ||{select_destination(&list,"search");search.show_groups();})}),
                ("installed-group", {let(list,installed)=(list.clone(),installed.clone());Box::new(move ||{select_destination(&list,"installed");installed.groups.set_selected(1);})}),
                ("news",Box::new(select("news"))),
                ("history", Box::new(select("history"))),
                ("details", {
                    let ctx = ctx.clone();
                    Box::new(move || {
                        crate::details::show(
                            &ctx,
                            crate::context::Item {
                                source: 0,
                                package: athena_core::PackageInfo {
                                    name: "firefox".into(),
                                    ..Default::default()
                                },
                            },
                        )
                    })
                }),
                ("preferences", {
                    let window = window.clone();
                    Box::new(move || {
                        if let Some(settings) = crate::settings::load() {
                            let dialog = crate::settings::dialog(&settings);
                            crate::settings::add_tools(&dialog, window.upcast_ref());
                            dialog.present(Some(&window));
                        }
                    })
                }),
                ("versions", {
                    let ctx = ctx.clone();
                    Box::new(move || {
                        if ctx.backends[0].key() == "pacman" {
                            crate::versions::show(&ctx, "linux");
                        }
                    })
                }),
                ("permissions", {
                    let ctx = ctx.clone();
                    Box::new(move || {
                        if let Some(source) = ctx.backends.iter().position(|b| b.key() == "flatpak") {
                            let backend = ctx.backends[source].clone();
                            let ctx = ctx.clone();
                            crate::util::background(
                                move || backend.list_installed().into_iter().next(),
                                move |package| {
                                    if let Some(package) = package {
                                        crate::pages::flatpak::show_app(&ctx, &package.name);
                                    }
                                },
                            );
                        }
                    })
                }),
                ("setup", {
                    let ctx = ctx.clone();
                    Box::new(move || crate::setup::show(&ctx))
                }),
                ("narrow-discover", {
                    let (window, list) = (window.clone(), list.clone());
                    Box::new(move || {
                        window.set_default_size(420, 760);
                        select_destination(&list,"discover");
                    })
                }),
                ("narrow-flatpak", {
                    let (window, list) = (window.clone(), list.clone());
                    Box::new(move || {
                        window.set_default_size(420, 760);
                        select_destination(&list,"flatpak");
                    })
                }),
                ("narrow-arch", {
                    let (window, list) = (window.clone(), list.clone());
                    Box::new(move || {
                        window.set_default_size(420, 760);
                        select_destination(&list,"arch");
                    })
                }),
                ("narrow-review", {
                    let (window, ctx) = (window.clone(), ctx.clone());
                    Box::new(move || {
                        window.set_default_size(420, 760);
                        crate::aur_review::screenshot(&ctx);
                    })
                }),
                ("narrow", Box::new(narrow)),
            ],
        );
    }

    let setup = gio::SimpleAction::new("setup", None);
    {
        let ctx = ctx.clone();
        setup.connect_activate(move |_, _| crate::setup::show(&ctx));
    }
    window.add_action(&setup);
    if crate::settings::load().is_some_and(|s| !s.boolean("setup-complete")) && std::env::var_os("ATHENA_SCREENSHOTS").is_none() {
        let ctx = ctx.clone();
        gtk::glib::idle_add_local_once(move || crate::setup::show(&ctx));
    }
    let shortcuts = gio::SimpleAction::new("shortcuts", None);
    {
        let window = window.clone();
        shortcuts.connect_activate(move |_, _| {
            let dialog = adw::Dialog::builder()
                .title(crate::i18n::gettext("Keyboard Shortcuts"))
                .content_width(460)
                .build();
            let page = adw::PreferencesPage::new();
            let group = adw::PreferencesGroup::new();
            for (action, key) in [
                ("Find packages", "Ctrl+F"),
                ("Reload package information", "Ctrl+R"),
                ("Preferences", "Ctrl+,"),
                ("Keyboard shortcuts", "Ctrl+?"),
                ("Close window", "Ctrl+W"),
                ("Quit", "Ctrl+Q"),
                ("Select multiple packages", "Ctrl+click / Shift+click"),
            ] {
                group.add(&adw::ActionRow::builder().title(action).subtitle(key).build());
            }
            page.add(&group);
            dialog.set_child(Some(&page));
            dialog.present(Some(&window));
        });
    }
    window.add_action(&shortcuts);
    app.set_accels_for_action("win.shortcuts", &["<Control>question"]);
    app.set_accels_for_action("app.quit", &["<Control>q"]);
    app.set_accels_for_action("window.close", &["<Control>w"]);

    window
}
