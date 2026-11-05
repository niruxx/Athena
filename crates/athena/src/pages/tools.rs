//! Cleanup, portable backups, package lists and system snapshots.
use crate::{actions, context::Context, util};
use adw::prelude::*;
use athena_core::{backup, cleanup, snapshots::Provider, transfer};
use std::{cell::RefCell, path::PathBuf, rc::Rc};

fn button(group: &adw::PreferencesGroup, title: &str, subtitle: &str, label: &str, clicked: impl Fn() + 'static) {
    let row = adw::ActionRow::builder().use_markup(false).build();
    row.set_title(title);
    row.set_subtitle(subtitle);
    let button = gtk::Button::builder().label(label).valign(gtk::Align::Center).build();
    button.connect_clicked(move |_| clicked());
    row.add_suffix(&button);
    group.add(&row);
}
fn page(title: &str) -> (adw::ToolbarView, adw::PreferencesPage) {
    let view = adw::ToolbarView::new();
    view.add_top_bar(&adw::HeaderBar::builder().title_widget(&adw::WindowTitle::new(title, "")).build());
    let page = adw::PreferencesPage::new();
    view.set_content(Some(&page));
    (view, page)
}
fn push(ctx: &Rc<Context>, title: &str, widget: &impl IsA<gtk::Widget>) {
    ctx.navigation.push(&adw::NavigationPage::builder().title(title).child(widget).build());
}
/// These destructive previews deliberately remain mandatory even when ordinary
/// package confirmations have been disabled in Preferences.
fn preview(ctx: &Rc<Context>, title: &str, text: &str, verb: &str, accepted: impl FnOnce() + 'static) {
    let dialog = adw::AlertDialog::new(Some(title), Some(&crate::i18n::gettext("Review the exact changes below before continuing.")));
    let buffer = gtk::TextBuffer::new(None);
    buffer.set_text(text);
    let view = gtk::TextView::builder()
        .buffer(&buffer)
        .editable(false)
        .cursor_visible(false)
        .monospace(true)
        .wrap_mode(gtk::WrapMode::WordChar)
        .build();
    dialog.set_extra_child(Some(
        &gtk::ScrolledWindow::builder()
            .child(&view)
            .min_content_height(200)
            .max_content_height(400)
            .propagate_natural_height(true)
            .build(),
    ));
    dialog.add_responses(&[("cancel", &crate::i18n::gettext("Cancel")), ("apply", verb)]);
    dialog.set_close_response("cancel");
    dialog.set_response_appearance("apply", adw::ResponseAppearance::Destructive);
    let accepted = RefCell::new(Some(accepted));
    dialog.connect_response(Some("apply"), move |_, _| {
        if let Some(f) = accepted.borrow_mut().take() {
            f();
        }
    });
    dialog.present(Some(&ctx.window));
}
fn result(ctx: &Rc<Context>, work: impl FnOnce() -> Result<String, String> + Send + 'static) {
    let ctx = ctx.clone();
    util::background(work, move |result| match result {
        Ok(message) => {
            ctx.toast(&message);
            ctx.packages_changed();
        }
        Err(e) => ctx.toast(&e),
    });
}
fn folder(ctx: &Rc<Context>, title: &str, done: impl FnOnce(PathBuf) + 'static) {
    let ctx2 = ctx.clone();
    gtk::FileDialog::builder()
        .title(title)
        .build()
        .select_folder(Some(&ctx.window), None::<&gtk::gio::Cancellable>, move |r| {
            if let Ok(f) = r {
                if let Some(p) = f.path() {
                    done(p);
                } else {
                    ctx2.toast(&crate::i18n::gettext("Choose a local folder."));
                }
            }
        });
}
fn file(ctx: &Rc<Context>, title: &str, save: bool, name: Option<&str>, done: impl FnOnce(PathBuf) + 'static) {
    let picker = gtk::FileDialog::builder().title(title).build();
    if let Some(name) = name {
        picker.set_initial_name(Some(name));
    }
    let ctx2 = ctx.clone();
    let callback = move |r: Result<gtk::gio::File, gtk::glib::Error>| {
        if let Ok(f) = r {
            if let Some(p) = f.path() {
                done(p);
            } else {
                ctx2.toast(&crate::i18n::gettext("Choose a local file."));
            }
        }
    };
    if save {
        picker.save(Some(&ctx.window), None::<&gtk::gio::Cancellable>, callback);
    } else {
        picker.open(Some(&ctx.window), None::<&gtk::gio::Cancellable>, callback);
    }
}
pub fn build(ctx: &Rc<Context>) -> adw::ToolbarView {
    let (view, page) = page(&crate::i18n::gettext("Tools"));
    let group = adw::PreferencesGroup::builder().title(crate::i18n::gettext("Storage and recovery")).build();
    let ctx2 = ctx.clone();
    button(
        &group,
        &crate::i18n::gettext("Free Up Space"),
        &crate::i18n::gettext("Inspect package caches, unused dependencies and your files."),
        &crate::i18n::gettext("Open"),
        move || show_cleanup(&ctx2),
    );
    let ctx2 = ctx.clone();
    button(
        &group,
        &crate::i18n::gettext("Backup & Restore"),
        &crate::i18n::gettext("Copy configuration and Flatpak app data, with a portable app list."),
        &crate::i18n::gettext("Open"),
        move || show_backup(&ctx2),
    );
    let ctx2 = ctx.clone();
    button(
        &group,
        &crate::i18n::gettext("Export & Import"),
        &crate::i18n::gettext("One JSON package list across your enabled sources."),
        &crate::i18n::gettext("Open"),
        move || show_transfer(&ctx2),
    );
    let ctx2 = ctx.clone();
    button(
        &group,
        &crate::i18n::gettext("Snapshots"),
        &crate::i18n::gettext("Manage configured Snapper or Timeshift snapshots."),
        &crate::i18n::gettext("Open"),
        move || show_snapshots(&ctx2),
    );
    page.add(&group);
    view
}
pub fn show_cleanup(ctx: &Rc<Context>) {
    let (view, page) = page(&crate::i18n::gettext("Free Up Space"));
    let packages=adw::PreferencesGroup::builder().title(crate::i18n::gettext("Packages")).description(crate::i18n::gettext("Cache sizes are upper bounds: retained versions and unreadable files affect reclaimed space. Dependency previews include the manager's size estimates when available.")).build();
    page.add(&packages);
    for backend in &ctx.backends {
        if backend.key() == "aur" || backend.key() == "snap" {
            continue;
        }
        let cache_row = adw::ActionRow::builder()
            .title(format!("{} cache", backend.name()))
            .subtitle(crate::i18n::gettext("Calculating…"))
            .build();
        if ["apt", "dnf", "pacman"].contains(&backend.key()) {
            packages.add(&cache_row);
            let key = backend.key();
            let ctx2 = ctx.clone();
            util::background(
                move || cleanup::cache(key),
                move |item| {
                    if let Some((area, commands)) = item {
                        cache_row.set_subtitle(&format!(
                            "{} · {} unreadable paths. Pacman keeps cached versions still needed by its cleanup policy.",
                            cleanup::size(area.bytes()),
                            area.errors
                        ));
                        let clear = gtk::Button::builder().label(crate::i18n::gettext("Clean…")).valign(gtk::Align::Center).build();
                        cache_row.add_suffix(&clear);
                        let ctx = ctx2.clone();
                        clear.connect_clicked(move |_| {
                            let (ctx2, commands) = (ctx.clone(), commands.clone());
                            let text = format!(
                                "Up to {} of cached downloads. Installed packages stay installed.\n\n{}",
                                cleanup::size(area.bytes()),
                                commands.iter().map(|c| c.display()).collect::<Vec<_>>().join("\n")
                            );
                            preview(
                                &ctx,
                                &crate::i18n::gettext("Clean package cache?"),
                                &text,
                                &crate::i18n::gettext("Clean"),
                                move || run_tool(&ctx2, "Clean package cache", commands),
                            );
                        });
                    }
                },
            );
        }
        let (ctx2, backend) = (ctx.clone(), backend.clone());
        let title = format!("{} unused dependencies", backend.name());
        button(
            &packages,
            &title,
            &crate::i18n::gettext("Resolve the package manager's removal plan before removing anything."),
            &crate::i18n::gettext("Inspect…"),
            move || {
                let (ctx, backend) = (ctx2.clone(), backend.clone());
                let key = backend.key();
                util::background(
                    move || (cleanup::unused_preview(key), backend.clean_unused_commands()),
                    move |(text, commands)| {
                        let ctx2 = ctx.clone();
                        preview(
                            &ctx,
                            &crate::i18n::gettext("Remove unused dependencies?"),
                            &text,
                            &crate::i18n::gettext("Remove"),
                            move || run_tool(&ctx2, "Remove unused dependencies", commands),
                        );
                    },
                );
            },
        );
    }
    let files=adw::PreferencesGroup::builder().title(crate::i18n::gettext("Your files")).description(crate::i18n::gettext("Only regular files you own are included. Close apps first: caches may be in use. Temporary files must be older than seven days. Refresh this page after cleaning.")).build();
    page.add(&files);
    let caution = adw::PreferencesGroup::builder()
        .title(crate::i18n::gettext("Worth a second look"))
        .description(crate::i18n::gettext(
            "Trash, history and recent-file lists may contain things you want to keep. Nothing is selected automatically.",
        ))
        .build();
    page.add(&caution);
    let ctx2 = ctx.clone();
    util::background(cleanup::user_areas, move |areas| match areas {
        Err(e) => ctx2.toast(&e),
        Ok(areas) => {
            for area in areas {
                let group = if area.caution { &caution } else { &files };
                let text = format!(
                    "{} · {} files · {} unreadable paths",
                    cleanup::size(area.bytes()),
                    area.files.len(),
                    area.errors
                );
                let ctx = ctx2.clone();
                button(group, &area.title.clone(), &text, &crate::i18n::gettext("Review…"), move || {
                    let text = format!(
                        "{}\n{} files, {}\n\n{}\n\nFiles changed after this preview are refused. Empty directories are left in place.",
                        area.title,
                        area.files.len(),
                        cleanup::size(area.bytes()),
                        area.files
                            .iter()
                            .map(|f| format!("{}  {}", cleanup::size(f.bytes), f.path.display()))
                            .collect::<Vec<_>>()
                            .join("\n")
                    );
                    let (ctx2, area) = (ctx.clone(), area.clone());
                    preview(
                        &ctx,
                        &crate::i18n::gettext("Permanently delete these files?"),
                        &text,
                        &crate::i18n::gettext("Delete"),
                        move || result(&ctx2, move || cleanup::delete(&area).map(|n| format!("Deleted {n} files."))),
                    );
                });
            }
        }
    });
    push(ctx, "Free Up Space", &view);
}
fn run_tool(ctx: &Rc<Context>, title: &str, commands: Vec<athena_core::Command>) {
    if commands.is_empty() {
        ctx.toast(&crate::i18n::gettext("Nothing to do."));
        return;
    }
    let ctx2 = ctx.clone();
    crate::terminal::run(&ctx.window, title, commands, crate::settings::boolean("close-on-success"), move |ok| {
        ctx2.toast(if ok { "Completed." } else { "Operation stopped. Check the output." });
        ctx2.packages_changed();
    });
}

pub fn show_transfer(ctx: &Rc<Context>) {
    let (view, page) = page(&crate::i18n::gettext("Export & Import"));
    let group=adw::PreferencesGroup::builder().title(crate::i18n::gettext("Portable package list")).description(crate::i18n::gettext("Native packages explicitly installed, Flatpak apps with their remote and installation scope, and Snap apps. Enable every source you want included before exporting. Package versions and repositories must still be available on the destination system.")).build();
    let ctx2 = ctx.clone();
    button(
        &group,
        &crate::i18n::gettext("Export installed choices"),
        &crate::i18n::gettext("Writes a versioned JSON file. Dependencies will be resolved during import."),
        &crate::i18n::gettext("Export…"),
        move || export(&ctx2),
    );
    let ctx2 = ctx.clone();
    button(
        &group,
        &crate::i18n::gettext("Import package list"),
        &crate::i18n::gettext("Shows missing packages to select. AUR packages always require source review."),
        &crate::i18n::gettext("Import…"),
        move || import(&ctx2),
    );
    page.add(&group);
    push(ctx, "Export & Import", &view);
}
fn export(ctx: &Rc<Context>) {
    let ctx2 = ctx.clone();
    file(ctx, "Export package list", true, Some("athena-packages.json"), move |path| {
        let backends = ctx2.backends.clone();
        result(&ctx2, move || {
            let text = transfer::Manifest::collect(&backends)?.json()?;
            std::fs::write(path, text).map_err(|e| e.to_string())?;
            Ok("Package list exported.".into())
        });
    });
}
fn import(ctx: &Rc<Context>) {
    let ctx2 = ctx.clone();
    file(ctx, "Import package list", false, None, move |path| {
        let backends = ctx2.backends.clone();
        util::background(
            move || {
                let meta = std::fs::metadata(&path).map_err(|e| e.to_string())?;
                if meta.len() > 16 * 1024 * 1024 {
                    return Err("Package list is too large".to_owned());
                }
                transfer::Manifest::parse(&std::fs::read_to_string(path).map_err(|e| e.to_string())?).map(|m| m.missing(&backends))
            },
            move |r| match r {
                Err(e) => ctx2.toast(&e),
                Ok(missing) => show_missing(&ctx2, missing),
            },
        );
    });
}
fn show_missing(ctx: &Rc<Context>, missing: Vec<(transfer::Package, Option<usize>)>) {
    let (view, page) = page(&crate::i18n::gettext("Choose Missing Packages"));
    let group = adw::PreferencesGroup::builder()
        .title(format!("{} missing packages", missing.len()))
        .description(crate::i18n::gettext(
            "Unavailable sources must be enabled first. Flatpak remotes must already exist. Select only packages you want to install.",
        ))
        .build();
    let mut choices = vec![];
    for (package, source) in missing {
        let row = adw::SwitchRow::builder()
            .use_markup(false)
            .title(&package.name)
            .subtitle(format!(
                "{} · {}{}",
                package.source,
                package.repository,
                if source.is_none() { " · source unavailable" } else { "" }
            ))
            .sensitive(source.is_some())
            .build();
        group.add(&row);
        choices.push((row, package, source));
    }
    let install = gtk::Button::with_label(&crate::i18n::gettext("Install Selected…"));
    install.add_css_class("suggested-action");
    group.add(&install);
    let ctx2 = ctx.clone();
    install.connect_clicked(move |_| {
        let ctx = &ctx2;
        let mut groups: Vec<(usize, Vec<String>)> = vec![];
        let mut scoped = vec![];
        for (row, p, source) in &choices {
            if !row.is_active() {
                continue;
            }
            if let Some(source) = source {
                if p.source == "flatpak" {
                    if let Some(commands) = athena_core::source_tools::flatpak_commands("install", &p.name, &p.repository, &p.reference) {
                        scoped.extend(commands);
                    }
                } else if let Some((_, names)) = groups.iter_mut().find(|(s, _)| s == source) {
                    names.push(p.name.clone());
                } else {
                    groups.push((*source, vec![p.name.clone()]));
                }
            }
        }
        actions::batch_with_prefix(ctx, actions::Kind::Install, groups, scoped);
    });
    page.add(&group);
    push(ctx, "Choose Missing Packages", &view);
}
pub fn show_backup(ctx: &Rc<Context>) {
    let (view, page) = page(&crate::i18n::gettext("Backup & Restore"));
    let group=adw::PreferencesGroup::builder().title(crate::i18n::gettext("Create a backup")).description(crate::i18n::gettext("Choose configuration folders and Flatpak app data. Close apps first for a consistent copy. Links and special files are skipped and reported; backups may contain secrets, so store them securely. Your enabled sources' app list is included.")).build();
    page.add(&group);
    let choices = Rc::new(RefCell::new(Vec::new()));
    let choices2 = choices.clone();
    let ctx2 = ctx.clone();
    util::background(
        move || cleanup::home().and_then(|home| backup::candidates(&home)),
        move |r| match r {
            Err(e) => ctx2.toast(&e),
            Ok(paths) => {
                for path in paths {
                    let row = adw::SwitchRow::builder()
                        .use_markup(false)
                        .active(path == std::path::Path::new(".config"))
                        .build();
                    row.set_title(&path.to_string_lossy());
                    group.add(&row);
                    choices2.borrow_mut().push((row, path));
                }
            }
        },
    );
    let output = adw::PreferencesGroup::builder()
        .title(crate::i18n::gettext("Save selected files"))
        .description(crate::i18n::gettext(
            "Backups never overwrite an existing path. Plain copies are saved in a new timestamped folder; compressed copies use .tar.gz.",
        ))
        .build();
    for compressed in [true, false] {
        let (ctx2, choices) = (ctx.clone(), choices.clone());
        button(
            &output,
            if compressed { "Compressed backup" } else { "Plain folder copy" },
            &crate::i18n::gettext("Includes configuration, selected app data and the app list."),
            &crate::i18n::gettext("Save…"),
            move || {
                let selected: Vec<_> = choices
                    .borrow()
                    .iter()
                    .filter(|(row, _)| row.is_active())
                    .map(|(_, path)| path.clone())
                    .collect();
                if selected.is_empty() {
                    ctx2.toast(&crate::i18n::gettext("Select files or folders first."));
                    return;
                }
                let (ctx, backends) = (ctx2.clone(), ctx2.backends.clone());
                let save = move |path: PathBuf| {
                    result(&ctx, move || {
                        let home = cleanup::home()?;
                        let packages = transfer::Manifest::collect(&backends)?;
                        let index = backup::create(&home, &selected, &path, compressed, packages)?;
                        Ok(format!("Backup saved. {} links or special files skipped.", index.skipped.len()))
                    });
                };
                if compressed {
                    file(&ctx2, "Save compressed backup", true, Some("athena-backup.tar.gz"), save);
                } else {
                    folder(&ctx2, "Choose backup parent folder", move |parent| {
                        let name = format!(
                            "athena-backup-{}",
                            std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_secs()
                        );
                        save(parent.join(name));
                    });
                }
            },
        );
    }
    page.add(&output);
    let restore=adw::PreferencesGroup::builder().title(crate::i18n::gettext("Restore a backup")).description(crate::i18n::gettext("Preview all paths and replacements first. The app list is shown separately and does not install packages automatically. Close affected applications before restoring.")).build();
    for compressed in [true, false] {
        let ctx2 = ctx.clone();
        button(
            &restore,
            if compressed { "Compressed backup" } else { "Plain folder backup" },
            &crate::i18n::gettext("Restore files into your home directory."),
            &crate::i18n::gettext("Choose…"),
            move || {
                let ctx = ctx2.clone();
                let choose = move |path: PathBuf| {
                    let ctx2 = ctx.clone();
                    let path2 = path.clone();
                    util::background(
                        move || cleanup::home().and_then(|home| backup::preview(&path2, &home)),
                        move |r| match r {
                            Err(e) => ctx2.toast(&e),
                            Ok(plan) => {
                                let text = format!(
                                    "{} files · {} to replace\n\n{}\n\n{} package choices are included. Use Export & Import to install missing apps.\nSkipped when backed up: {}",
                                    plan.entries.len(),
                                    plan.entries.iter().filter(|e| e.overwrite).count(),
                                    plan.entries
                                        .iter()
                                        .map(|e| format!(
                                            "{} {} ({})",
                                            if e.overwrite { "REPLACE" } else { "CREATE" },
                                            e.path.display(),
                                            cleanup::size(e.bytes)
                                        ))
                                        .collect::<Vec<_>>()
                                        .join("\n"),
                                    plan.index.packages.packages.len(),
                                    plan.index.skipped.join(", ")
                                );
                                let ctx3 = ctx2.clone();
                                preview(
                                    &ctx2,
                                    &crate::i18n::gettext("Restore these files?"),
                                    &text,
                                    &crate::i18n::gettext("Restore"),
                                    move || {
                                        result(&ctx3, move || {
                                            backup::restore(&path, &cleanup::home()?, &plan).map(|n| format!("Restored {n} files."))
                                        })
                                    },
                                );
                            }
                        },
                    );
                };
                if compressed {
                    file(&ctx2, "Open compressed backup", false, None, choose);
                } else {
                    folder(&ctx2, "Open plain backup folder", choose);
                }
            },
        );
    }
    // Extract just the included package choices for the normal import picker.
    for compressed in [true, false] {
        let ctx2 = ctx.clone();
        button(
            &restore,
            if compressed {
                "Apps from compressed backup"
            } else {
                "Apps from folder backup"
            },
            &crate::i18n::gettext("Inspect missing apps without restoring files."),
            &crate::i18n::gettext("Choose…"),
            move || {
                let (ctx, backends) = (ctx2.clone(), ctx2.backends.clone());
                let choose = move |path: PathBuf| {
                    let ctx2 = ctx.clone();
                    let backends = backends.clone();
                    util::background(
                        move || backup::preview(&path, &cleanup::home()?).map(|p| p.index.packages.missing(&backends)),
                        move |r| match r {
                            Ok(missing) => show_missing(&ctx2, missing),
                            Err(e) => ctx2.toast(&e),
                        },
                    );
                };
                if compressed {
                    file(&ctx2, "Choose backup", false, None, choose);
                } else {
                    folder(&ctx2, "Choose backup folder", choose);
                }
            },
        );
    }
    page.add(&restore);
    push(ctx, "Backup & Restore", &view);
}
pub fn show_snapshots(ctx: &Rc<Context>) {
    let (view, page) = page(&crate::i18n::gettext("Snapshots"));
    let group=adw::PreferencesGroup::builder().title(crate::i18n::gettext("System snapshots")).description(crate::i18n::gettext("Uses the configured root Snapper profile or Timeshift. Configure the tool before use. Snapshots cover only the filesystems included in that tool's configuration.")).build();
    page.add(&group);
    let Some(provider) = Provider::detect() else {
        group.add(
            &adw::ActionRow::builder()
                .title(crate::i18n::gettext("No snapshot provider found"))
                .subtitle(crate::i18n::gettext("Install and configure Snapper or Timeshift."))
                .build(),
        );
        push(ctx, "Snapshots", &view);
        return;
    };
    let listing = gtk::TextBuffer::new(None);
    listing.set_text(if provider == Provider::Timeshift {
        "Timeshift listing requires administrator access. Use the button below."
    } else {
        "Select Refresh to list snapshots without administrator access."
    });
    let text = gtk::TextView::builder()
        .buffer(&listing)
        .editable(false)
        .cursor_visible(false)
        .monospace(true)
        .wrap_mode(gtk::WrapMode::WordChar)
        .build();
    group.add(
        &gtk::ScrolledWindow::builder()
            .child(&text)
            .min_content_height(180)
            .max_content_height(320)
            .propagate_natural_height(true)
            .build(),
    );
    let target = adw::EntryRow::builder()
        .title(if provider == Provider::Snapper {
            "Snapshot number"
        } else {
            "Snapshot name (YYYY-MM-DD_HH-MM-SS)"
        })
        .build();
    let model = gtk::StringList::new(&[]);
    let selector = gtk::DropDown::builder().model(&model).build();
    group.add(&selector);
    let ids = Rc::new(RefCell::new(Vec::<String>::new()));
    let (ids2, target2) = (ids.clone(), target.clone());
    selector.connect_selected_notify(move |picker| {
        if let Some(id) = ids2.borrow().get(picker.selected() as usize) {
            target2.set_text(id);
        }
    });
    let target2 = target.clone();
    let ctx2 = ctx.clone();
    button(
        &group,
        provider.name(),
        if provider == Provider::Timeshift {
            "Authentication is requested only when you choose to list."
        } else {
            "Listing uses your configured Snapper permissions."
        },
        if provider == Provider::Timeshift {
            "List as Administrator…"
        } else {
            "Refresh"
        },
        move || {
            let command = provider.list_command();
            let ctx = ctx2.clone();
            let listing = listing.clone();
            let (model, ids, selector, target) = (model.clone(), ids.clone(), selector.clone(), target2.clone());
            util::background(
                move || athena_core::process::run_sequence(&[command], athena_core::process::LONG),
                move |out| {
                    if !out.success() {
                        ctx.toast(&crate::i18n::gettext("Could not list snapshots. Check tool configuration and permissions."));
                    }
                    listing.set_text(&out.combined());
                    if out.success() {
                        athena_core::snapshots::record_listing(provider, &out.stdout);
                        let snapshots = athena_core::snapshots::parse_listing(provider, &out.stdout);
                        let labels = snapshots
                            .iter()
                            .map(|s| format!("{} · {} · {}", s.id, s.date, s.description))
                            .collect::<Vec<_>>();
                        *ids.borrow_mut() = snapshots.iter().map(|s| s.id.clone()).collect();
                        model.splice(0, model.n_items(), &labels.iter().map(String::as_str).collect::<Vec<_>>());
                        selector.set_selected(0);
                        if let Some(first) = snapshots.first() {
                            target.set_text(&first.id);
                        }
                        ctx.packages_changed();
                    }
                },
            );
        },
    );
    let ctx2 = ctx.clone();
    button(
        &group,
        &crate::i18n::gettext("Take a snapshot"),
        &crate::i18n::gettext("Creates one snapshot with the description “Athena manual snapshot”."),
        &crate::i18n::gettext("Take…"),
        move || {
            actions::custom(
                &ctx2,
                "Take snapshot?",
                "Uses the snapshot tool's configured filesystems and destination.",
                vec![provider.create("Athena manual snapshot")],
            )
        },
    );
    group.add(&target);
    for (action, label, explanation) in if provider == Provider::Snapper {
        vec![
            ("delete", "Delete…", "Permanently deletes this snapshot."),
            (
                "rollback",
                "Rollback…",
                "Creates a new default Btrfs subvolume from this snapshot. Restart to boot it. Requires a rollback-capable configuration.",
            ),
            (
                "undo",
                "Undo Changes…",
                "Reverts ALL changes between this snapshot and the current filesystem, immediately. Save work and close applications first.",
            ),
        ]
    } else {
        vec![
            ("delete", "Delete…", "Permanently deletes this snapshot."),
            (
                "restore",
                "Restore…",
                "Opens Timeshift. Select the snapshot name entered here, then Restore to choose target disks and bootloader settings.",
            ),
        ]
    } {
        let (ctx2, target) = (ctx.clone(), target.clone());
        button(&group, label, explanation, label, move || {
            match provider.command(action, target.text().trim()) {
                Err(e) => ctx2.toast(&e),
                Ok(command) => {
                    let ctx = ctx2.clone();
                    preview(
                        &ctx2,
                        label,
                        &format!("{explanation}\n\n{}", command.display()),
                        &crate::i18n::gettext("Continue"),
                        move || run_tool(&ctx, label, vec![command]),
                    );
                }
            }
        });
    }
    push(ctx, "Snapshots", &view);
}

#[cfg(debug_assertions)]
pub fn screenshot_import(ctx: &Rc<Context>) {
    show_missing(
        ctx,
        vec![
            (
                transfer::Package {
                    source: "flatpak".into(),
                    name: "org.example.BackupPreview".into(),
                    repository: "user:flathub".into(),
                    reference: "app/org.example.BackupPreview/x86_64/stable".into(),
                },
                ctx.backends.iter().position(|b| b.key() == "flatpak"),
            ),
            (
                transfer::Package {
                    source: "aur".into(),
                    name: "athena-preview-package".into(),
                    repository: String::new(),
                    reference: String::new(),
                },
                ctx.backends.iter().position(|b| b.key() == "aur"),
            ),
        ],
    );
}
