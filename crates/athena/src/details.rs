//! Package details loaded away from the GTK thread.
use crate::{
    actions::{self, Kind},
    context::{Context, Item},
    util,
};
use adw::prelude::*;
use gtk::gio;
use std::rc::Rc;

pub fn show(ctx: &Rc<Context>, item: Item) {
    show_with_app(ctx, item, None);
}
fn show_with_app(ctx: &Rc<Context>, item: Item, catalog_app: Option<athena_core::catalog::App>) {
    let toolbar = adw::ToolbarView::new();
    toolbar.add_top_bar(&adw::HeaderBar::new());
    toolbar.set_content(Some(&util::loading_page("Loading package details…")));
    let page = adw::NavigationPage::builder().title(&item.package.name).child(&toolbar).build();
    ctx.navigation.push(&page);
    let generation = Rc::new(std::cell::Cell::new(0u64));
    load(ctx, &toolbar, item.clone(), &generation);
    let selected = Rc::new(std::cell::RefCell::new(item.clone()));
    {
        let (weak_ctx, weak_toolbar, selected, generation) = (Rc::downgrade(ctx), toolbar.downgrade(), selected.clone(), generation.clone());
        ctx.on_packages_changed(move || {
            if let (Some(ctx), Some(toolbar)) = (weak_ctx.upgrade(), weak_toolbar.upgrade()) {
                load(&ctx, &toolbar, selected.borrow().clone(), &generation);
            }
        });
    }
    let (backends, name) = (ctx.backends.clone(), item.package.name.clone());
    let (ctx, toolbar) = (ctx.clone(), toolbar.clone());
    util::background(
        move || {
            let catalog = athena_core::catalog::current();
            let mut alternatives = Vec::new();
            if let Some(app) = catalog_app.as_ref().or_else(|| catalog.find(&name)) {
                for offer in &app.offers {
                    if let Some(source) = backends.iter().position(|b| b.key() == offer.backend) {
                        alternatives.push(Item {
                            source,
                            package: athena_core::catalog::package(app, offer),
                        });
                    }
                }
                let present = alternatives.iter().map(|item| item.source).collect::<std::collections::HashSet<_>>();
                let mut names = app.offers.iter().map(|offer| offer.package.clone()).collect::<Vec<_>>();
                names.push(app.id.trim_end_matches(".desktop").to_owned());
                names.sort();
                names.dedup();
                for (source, backend) in backends.iter().enumerate().filter(|(source, _)| !present.contains(source)) {
                    let mut candidates = names.clone();
                    if backend.key() == "aur" {
                        candidates.extend(
                            names
                                .iter()
                                .filter(|name| !name.contains('.') && !name.ends_with("-bin"))
                                .map(|name| format!("{name}-bin")),
                        );
                    }
                    alternatives.extend(
                        backend
                            .package_details(&candidates)
                            .into_iter()
                            .filter(|package| candidates.contains(&package.name))
                            .map(|package| Item { source, package }),
                    );
                }
            }
            if alternatives.is_empty() {
                let metadata = athena_core::media::metadata(&name);
                let mut names = metadata.packages;
                if !metadata.id.is_empty() {
                    names.push(metadata.id);
                }
                names.push(name);
                names.sort();
                names.dedup();
                alternatives = backends
                    .iter()
                    .enumerate()
                    .flat_map(|(source, backend)| {
                        backend
                            .package_details(&names)
                            .into_iter()
                            .filter(|p| names.contains(&p.name))
                            .map(move |package| Item { source, package })
                    })
                    .collect();
            }
            alternatives
        },
        move |mut alternatives| {
            if !alternatives.iter().any(|other| {
                other.source == item.source
                    && other.package.name == item.package.name
                    && other.package.repository == item.package.repository
                    && other.package.install_ref == item.package.install_ref
            }) {
                alternatives.push(item.clone());
            }
            if alternatives.len() < 2 {
                return;
            }
            let labels = alternatives
                .iter()
                .map(|i| {
                    format!(
                        "{} · {} · {}{} · {}",
                        ctx.source_name(i.source),
                        i.package.repository,
                        i.package.architecture,
                        i.package.install_ref.split('/').nth(3).map(|branch| format!(" / {branch}")).unwrap_or_default(),
                        i.package.name
                    )
                })
                .collect::<Vec<_>>();
            let labels = labels.iter().map(String::as_str).collect::<Vec<_>>();
            let picker = gtk::DropDown::from_strings(&labels);
            picker.set_selected(
                alternatives
                    .iter()
                    .position(|i| {
                        i.source == item.source
                            && i.package.name == item.package.name
                            && i.package.repository == item.package.repository
                            && i.package.install_ref == item.package.install_ref
                    })
                    .unwrap_or(0) as u32,
            );
            picker.set_tooltip_text(Some("Install from / package source"));
            toolbar.add_top_bar(&picker);
            picker.connect_selected_notify(move |picker| {
                if let Some(item) = alternatives.get(picker.selected() as usize) {
                    *selected.borrow_mut() = item.clone();
                    load(&ctx, &toolbar, item.clone(), &generation);
                }
            });
        },
    );
}

fn load(ctx: &Rc<Context>, toolbar: &adw::ToolbarView, item: Item, generation: &Rc<std::cell::Cell<u64>>) {
    generation.set(generation.get() + 1);
    let current = generation.get();
    let generation = generation.clone();
    let toolbar = toolbar.clone();
    let backend = ctx.backends[item.source].clone();
    let name = item.package.name.clone();
    let scope = item.package.repository.split_once(':').map(|(scope, _)| scope.to_owned());
    let reference = if item.package.install_ref.is_empty() {
        name.clone()
    } else {
        item.package.install_ref.clone()
    };
    let scoped = backend.key() == "flatpak";
    let ctx = ctx.clone();
    util::background(
        move || {
            let metadata = athena_core::media::metadata(&name);
            let scoped_info = scope
                .filter(|scope| scoped && (matches!(scope.as_str(), "user" | "system") || scope.starts_with("installation=")))
                .map(|scope| athena_core::process::run("flatpak", &["info", &format!("--{scope}"), "--", &reference], athena_core::process::SHORT));
            (backend.package_details(&[name]), metadata, scoped_info)
        },
        move |(packages, metadata, scoped_info)| {
            if generation.get() != current {
                return;
            }
            let selected_repository = item.package.repository.clone();
            let selected_reference = item.package.install_ref.clone();
            let selected_architecture = item.package.architecture.clone();
            let mut package = packages.into_iter().next().unwrap_or(item.package);
            package.install_ref = selected_reference;
            if ctx.backends[item.source].key() == "flatpak" && !selected_architecture.is_empty() {
                package.architecture = selected_architecture;
            }
            if let Some(info) = scoped_info {
                package.installed = info.success();
                package.installed_version = if info.success() {
                    info.stdout
                        .lines()
                        .find_map(|l| l.trim().strip_prefix("Version:").map(|v| v.trim().to_owned()))
                        .unwrap_or_default()
                } else {
                    String::new()
                };
            }
            if ctx.backends[item.source].key() == "flatpak" && selected_repository.contains(':') {
                package.repository = selected_repository;
            }
            if package.long_description.is_empty() {
                package.long_description = metadata.description;
            }
            if package.homepage_url.is_empty() {
                package.homepage_url = metadata.homepage;
            }
            let body = adw::PreferencesPage::new();
            let group = adw::PreferencesGroup::builder()
                .title(gtk::glib::markup_escape_text(&package.name))
                .description(gtk::glib::markup_escape_text(&package.description))
                .build();
            let buttons = gtk::FlowBox::builder()
                .selection_mode(gtk::SelectionMode::None)
                .column_spacing(8)
                .row_spacing(8)
                .min_children_per_line(1)
                .max_children_per_line(5)
                .halign(gtk::Align::Center)
                .margin_top(12)
                .margin_bottom(12)
                .build();
            for (label, kind) in if package.installed {
                vec![("Update", Kind::Update), ("Reinstall", Kind::Reinstall), ("Remove…", Kind::Remove)]
            } else {
                vec![("Install", Kind::Install)]
            } {
                let button = gtk::Button::with_label(label);
                let (ctx, selected) = (
                    ctx.clone(),
                    Item {
                        source: item.source,
                        package: package.clone(),
                    },
                );
                button.connect_clicked(move |_| {
                    actions::transaction_item(&ctx, selected.clone(), kind);
                });
                buttons.insert(&button, -1);
            }
            if let Some(app) = package.installed.then(|| crate::desktop::find(&package.name)).flatten() {
                let open = gtk::Button::with_label(&crate::i18n::gettext("Open"));
                let ctx = ctx.clone();
                open.connect_clicked(move |_| {
                    if let Err(error) = app.launch(&[], None::<&gio::AppLaunchContext>) {
                        ctx.toast(&error.to_string());
                    }
                });
                buttons.insert(&open, -1);
            }
            if ctx.backends[item.source].key() == "pacman" {
                let other = gtk::Button::with_label(&crate::i18n::gettext("Other Versions…"));
                let (ctx, name) = (ctx.clone(), package.name.clone());
                other.connect_clicked(move |_| crate::versions::show(&ctx, &name));
                buttons.insert(&other, -1);
            }
            if ctx.backends[item.source].key() == "snap" && package.installed {
                let manage = gtk::Button::with_label(&crate::i18n::gettext("Manage Snap"));
                let (ctx, id) = (ctx.clone(), package.name.clone());
                manage.connect_clicked(move |_| crate::pages::snap::show(&ctx, &id));
                buttons.insert(&manage, -1);
            }
            if matches!(ctx.backends[item.source].key(), "pacman" | "aur") {
                let files = gtk::Button::with_label(&crate::i18n::gettext("View Build Files"));
                let (ctx, name, is_aur) = (ctx.clone(), package.name.clone(), ctx.backends[item.source].key() == "aur");
                files.connect_clicked(move |_| crate::pages::arch::build_files(&ctx, &name, is_aur));
                buttons.insert(&files, -1);
            }
            group.add(&buttons);
            for (title, value) in [
                ("Description", &package.long_description),
                ("Installed version", &package.installed_version),
                ("Available version", &package.available_version),
                ("Architecture", &package.architecture),
                ("Size", &package.size),
                ("Repository", &package.repository),
            ] {
                if !value.is_empty() {
                    group.add(
                        &adw::ActionRow::builder()
                            .title(title)
                            .subtitle(gtk::glib::markup_escape_text(value))
                            .subtitle_selectable(true)
                            .build(),
                    );
                }
            }
            if package.homepage_url.starts_with("https://") || package.homepage_url.starts_with("http://") {
                group.add(&gtk::LinkButton::with_label(&package.homepage_url, "Homepage"));
            }
            group.add(
                &adw::ActionRow::builder()
                    .title(crate::i18n::gettext("Source"))
                    .subtitle(ctx.source_name(item.source))
                    .build(),
            );
            body.add(&group);
            let screenshots = adw::PreferencesGroup::builder().title(crate::i18n::gettext("Screenshots")).build();
            let images = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(12).build();
            let message = gtk::Label::new(Some("Loading screenshots…"));
            images.append(&message);
            screenshots.add(&images);
            body.add(&screenshots);
            let cache = gtk::glib::user_cache_dir().join("athena/screenshots");
            util::background(
                move || {
                    metadata
                        .screenshots
                        .iter()
                        .filter_map(|url| athena_core::media::cache_image(url, &cache))
                        .collect::<Vec<_>>()
                },
                move |paths| {
                    images.remove(&message);
                    if paths.is_empty() {
                        images.append(&gtk::Label::new(Some("No screenshots available from this source.")));
                    }
                    for path in paths {
                        images.append(&gtk::Picture::builder().file(&gio::File::for_path(path)).can_shrink(true).build());
                    }
                },
            );
            if ctx.backends[item.source].key() == "flatpak" && package.installed {
                let permissions = adw::PreferencesGroup::builder().title(crate::i18n::gettext("Sandbox permissions")).build();
                let target = permissions.clone();
                let name = package.name.clone();
                util::background(
                    move || athena_core::backends::FlatpakBackend::permissions(&name),
                    move |p| {
                        for (title, values) in [
                            ("Network and shared resources", p.shared.into_iter().collect::<Vec<_>>()),
                            ("Sockets", p.sockets.into_iter().collect()),
                            ("Devices", p.devices.into_iter().collect()),
                            ("Files", p.filesystems),
                            ("Session bus", p.session_bus_talk),
                            ("System bus", p.system_bus_talk),
                        ] {
                            if values.is_empty() {
                                continue;
                            }
                            let row = adw::ActionRow::builder()
                                .title(title)
                                .subtitle(gtk::glib::markup_escape_text(&values.join(", ")))
                                .build();
                            if values.iter().any(|v| {
                                matches!(v.split(':').next().unwrap_or(v), "host" | "home" | "all" | "system-bus" | "session-bus" | "x11")
                                    || v == "org.freedesktop.Flatpak"
                            }) {
                                row.add_css_class("warning");
                            }
                            target.add(&row);
                        }
                    },
                );
                let edit = gtk::Button::with_label(&crate::i18n::gettext("Edit Permissions"));
                let (ctx2, name) = (ctx.clone(), package.name.clone());
                edit.connect_clicked(move |_| crate::pages::flatpak::show_app(&ctx2, &name));
                permissions.add(&edit);
                body.add(&permissions);
            }
            toolbar.set_content(Some(&body));
        },
    );
}

pub fn show_catalog_app(ctx: &Rc<Context>, app: &athena_core::catalog::App) {
    if let Some((source, offer)) = app
        .offers
        .iter()
        .find_map(|offer| ctx.backends.iter().position(|b| b.key() == offer.backend).map(|source| (source, offer)))
    {
        show_with_app(
            ctx,
            Item {
                source,
                package: athena_core::catalog::package(app, offer),
            },
            Some(app.clone()),
        );
    } else {
        ctx.toast(&crate::i18n::gettext("This application's source is not enabled."));
    }
}
