//! Installing, removing, reinstalling, and updating: a confirmation with
//! the resolved plan, then the commands in the terminal window.

use std::rc::Rc;

use adw::prelude::*;
use athena_core::{Command, TransactionPreview};

use crate::context::Context;
use crate::{terminal, util};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Install,
    Remove,
    Reinstall,
    Update,
}

impl Kind {
    fn verb(self) -> &'static str {
        match self {
            Kind::Install => "Install",
            Kind::Remove => "Remove",
            Kind::Reinstall => "Reinstall",
            Kind::Update => "Update",
        }
    }

    fn done(self) -> &'static str {
        match self {
            Kind::Install => "Installed",
            Kind::Remove => "Removed",
            Kind::Reinstall => "Reinstalled",
            Kind::Update => "Updated",
        }
    }
}

fn summarize(names: &[String]) -> String {
    match names {
        [one] => one.clone(),
        [a, b] => format!("{a} and {b}"),
        _ => format!("{} packages", names.len()),
    }
}

/// A confirmation dialog whose plan fills in once `plan` is computed.
fn confirm(
    ctx: &Rc<Context>,
    heading: &str,
    body: &str,
    verb: &str,
    destructive: bool,
    plan: Option<Box<dyn FnOnce() -> TransactionPreview + Send>>,
    accepted: impl FnOnce() + 'static,
) {
    if crate::settings::boolean("skip-confirmations") {
        accepted();
        return;
    }
    let dialog = adw::AlertDialog::new(Some(heading), Some(body));
    dialog.add_response("cancel", &crate::i18n::gettext("Cancel"));
    dialog.add_response("ok", verb);
    dialog.set_response_appearance(
        "ok",
        if destructive {
            adw::ResponseAppearance::Destructive
        } else {
            adw::ResponseAppearance::Suggested
        },
    );
    dialog.set_default_response(Some("ok"));
    dialog.set_close_response("cancel");

    if let Some(plan) = plan {
        let stack = gtk::Stack::new();
        let spinner = gtk::Spinner::builder().spinning(true).halign(gtk::Align::Center).margin_top(6).build();
        stack.add_named(&spinner, Some("loading"));
        let text = gtk::Label::builder().xalign(0.0).wrap(true).build();
        text.add_css_class("monospace");
        text.add_css_class("caption");
        let scroller = gtk::ScrolledWindow::builder()
            .child(&text)
            .max_content_height(260)
            .propagate_natural_height(true)
            .build();
        stack.add_named(&scroller, Some("plan"));
        dialog.set_extra_child(Some(&stack));
        dialog.set_response_enabled("ok", false);
        let dialog_ref = dialog.downgrade();
        util::background(plan, move |preview| {
            let Some(dialog) = dialog_ref.upgrade() else { return };
            dialog.set_response_enabled("ok", true);
            if preview.available && !preview.plan_text.is_empty() {
                text.set_label(&preview.plan_text);
                stack.set_visible_child_name("plan");
            } else {
                stack.set_visible(false);
            }
        });
    }

    let accepted = std::cell::RefCell::new(Some(accepted));
    dialog.connect_response(None, move |_, response| {
        if response == "ok"
            && let Some(accepted) = accepted.borrow_mut().take()
        {
            accepted();
        }
    });
    dialog.present(Some(&ctx.window));
}

/// Runs commands computed off the main thread (Flatpak looks up each app's
/// remote, for instance) in the terminal window, then refreshes.
fn run_commands(ctx: &Rc<Context>, title: String, done_message: String, commands: impl FnOnce() -> Vec<Command> + Send + 'static) {
    run_commands_then(ctx, title, done_message, commands, || {});
}
fn run_commands_then(
    ctx: &Rc<Context>,
    title: String,
    done_message: String,
    commands: impl FnOnce() -> Vec<Command> + Send + 'static,
    after: impl FnOnce() + 'static,
) {
    let ctx = ctx.clone();
    util::background(commands, move |commands| {
        if commands.is_empty() {
            ctx.toast(&crate::i18n::gettext("Nothing to do."));
            after();
            return;
        }
        let ctx2 = ctx.clone();
        terminal::run(&ctx.window, &title, commands, crate::settings::boolean("close-on-success"), move |ok| {
            ctx2.toast(if ok {
                &done_message
            } else {
                "Something went wrong — nothing may have changed."
            });
            ctx2.packages_changed();
            if ok {
                after();
            }
        });
    });
}

/// Install, remove, reinstall, or update `names` from one source. An update
/// with no names is the source's own full update (`pacman -Syu`…).
pub fn transaction(ctx: &Rc<Context>, source: usize, kind: Kind, names: Vec<String>) {
    if ctx.backends[source].key() == "aur" && kind != Kind::Remove {
        if names.is_empty() {
            let backend = ctx.backends[source].clone();
            let ctx = ctx.clone();
            util::background(
                move || backend.list_updates().into_iter().map(|p| p.name).collect::<Vec<_>>(),
                move |names| {
                    if !names.is_empty() {
                        crate::aur_review::review(&ctx, names);
                    }
                },
            );
        } else {
            crate::aur_review::review(ctx, names);
        }
        return;
    }
    if names.is_empty() && kind != Kind::Update {
        return;
    }
    if kind == Kind::Update && names.is_empty() && ctx.backends[source].key() == "pacman" {
        let ctx2 = ctx.clone();
        crate::pages::arch::gate_update(ctx, move || transaction_ready(&ctx2, source, kind, names));
        return;
    }
    transaction_ready(ctx, source, kind, names);
}
fn transaction_ready(ctx: &Rc<Context>, source: usize, kind: Kind, names: Vec<String>) {
    let backend = ctx.backends[source].clone();
    let what = if names.is_empty() {
        format!("everything from {}", ctx.source_name(source))
    } else {
        summarize(&names)
    };
    let heading = format!("{} {what}?", kind.verb());
    let body = match kind {
        Kind::Install => format!("From {}. You'll be asked for your password.", ctx.source_name(source)),
        Kind::Remove => "Dependencies nothing else needs are removed too.".to_owned(),
        Kind::Reinstall => "Downloads and installs the same version again.".to_owned(),
        Kind::Update => "Installs the newer version.".to_owned(),
    };
    let plan: Option<Box<dyn FnOnce() -> TransactionPreview + Send>> = match kind {
        Kind::Install => {
            let (b, n) = (backend.clone(), names.clone());
            Some(Box::new(move || b.preview_install(&n)))
        }
        Kind::Remove => {
            let (b, n) = (backend.clone(), names.clone());
            Some(Box::new(move || b.preview_remove(&n)))
        }
        _ => None,
    };
    let ctx2 = ctx.clone();
    let title = format!("{} {what}", kind.verb());
    let done_message = format!("{} {what}", kind.done());
    confirm(ctx, &heading, &body, kind.verb(), kind == Kind::Remove, plan, move || {
        run_commands(&ctx2, title, done_message, move || match kind {
            Kind::Install => backend.install_commands(&names),
            Kind::Remove => backend.remove_commands(&names),
            Kind::Reinstall => backend.reinstall_commands(&names),
            Kind::Update => backend.upgrade_commands(&names),
        });
    });
}

/// Every source's full update (pacman -Syu, dnf upgrade, flatpak update,
/// snap refresh) in one terminal window. `counts` is each source's
/// pending update count, for the confirmation.
pub fn update_everything(ctx: &Rc<Context>, counts: Vec<(usize, usize)>) {
    if counts.iter().all(|(_, n)| *n == 0) {
        return;
    }
    let ctx2 = ctx.clone();
    crate::pages::arch::gate_update(ctx, move || update_everything_ready(&ctx2, counts));
}
fn update_everything_ready(ctx: &Rc<Context>, counts: Vec<(usize, usize)>) {
    let total: usize = counts.iter().map(|(_, n)| n).sum();
    if total == 0 {
        return;
    }
    let breakdown = counts
        .iter()
        .filter(|(_, n)| *n > 0)
        .map(|(source, n)| format!("{} {n}", ctx.source_name(*source)))
        .collect::<Vec<_>>()
        .join(", ");
    let aur = counts
        .iter()
        .find(|(source, count)| *count > 0 && ctx.backends[*source].key() == "aur")
        .map(|(source, _)| *source);
    let backends: Vec<_> = counts
        .iter()
        .filter(|(source, n)| *n > 0 && ctx.backends[*source].key() != "aur")
        .map(|(s, _)| ctx.backends[*s].clone())
        .collect();
    let needs_aur_snapshot = backends.is_empty();
    let ctx2 = ctx.clone();
    confirm(
        ctx,
        &format!("Install {total} update{}?", if total == 1 { "" } else { "s" }),
        &format!("{breakdown}. You'll be asked for your password."),
        &crate::i18n::gettext("Update"),
        false,
        None,
        move || {
            let ctx3 = ctx2.clone();
            run_commands_then(
                &ctx2,
                "Update everything".into(),
                "Updates installed".into(),
                move || backends.iter().flat_map(|b| b.upgrade_commands(&[])).collect(),
                move || {
                    if let Some(source) = aur {
                        let backend = ctx3.backends[source].clone();
                        let ctx4 = ctx3.clone();
                        util::background(
                            move || backend.list_updates().into_iter().map(|p| p.name).collect::<Vec<_>>(),
                            move |names| {
                                if !names.is_empty() {
                                    crate::aur_review::review_in_batch(&ctx4, names, needs_aur_snapshot);
                                }
                            },
                        );
                    }
                },
            );
        },
    );
}

/// Pick a destination before constructing any download commands.
pub fn download(ctx: &Rc<Context>, source: usize, names: Vec<String>) {
    download_batch(ctx, vec![(source, names)]);
}

pub fn download_batch(ctx: &Rc<Context>, groups: Vec<(usize, Vec<String>)>) {
    if groups.iter().all(|(_, names)| names.is_empty()) {
        return;
    }
    let dialog = adw::AlertDialog::new(
        Some(&crate::i18n::gettext("Download packages")),
        Some("Include dependencies? Flatpak downloads into its own cache; other sources use the selected folder."),
    );
    dialog.add_responses(&[
        ("cancel", &crate::i18n::gettext("Cancel")),
        ("only", &crate::i18n::gettext("Packages only")),
        ("dependencies", &crate::i18n::gettext("Include dependencies")),
    ]);
    dialog.set_close_response("cancel");
    let parent = ctx.window.clone();
    let ctx = ctx.clone();
    dialog.connect_response(None, move |_, response| {
        if response == "cancel" {
            return;
        }
        let dependencies = response == "dependencies";
        let (ctx, groups) = (ctx.clone(), groups.clone());
        let picker = gtk::FileDialog::builder().title(crate::i18n::gettext("Download to folder")).build();
        let parent = ctx.window.clone();
        picker.select_folder(Some(&parent), None::<&gtk::gio::Cancellable>, move |result| {
            let Ok(folder) = result else { return };
            let Some(path) = folder.path() else {
                ctx.toast(&crate::i18n::gettext("Choose a local folder."));
                return;
            };
            let backends = ctx.backends.clone();
            run_commands(&ctx, "Download packages".into(), "Download complete".into(), move || {
                groups
                    .iter()
                    .flat_map(|(source, names)| backends[*source].download_commands(names, &path.to_string_lossy(), dependencies))
                    .collect()
            });
        });
    });
    dialog.present(Some(&parent));
}

/// A batch is confirmed once and executed serially, including across sources.
pub fn batch(ctx: &Rc<Context>, kind: Kind, groups: Vec<(usize, Vec<String>)>) {
    batch_with_prefix(ctx, kind, groups, vec![]);
}
pub fn batch_with_prefix(ctx: &Rc<Context>, kind: Kind, groups: Vec<(usize, Vec<String>)>, prefix: Vec<Command>) {
    let mut aur_names = Vec::new();
    let groups: Vec<_> = groups
        .into_iter()
        .filter(|(source, names)| {
            if ctx.backends[*source].key() == "aur" && kind != Kind::Remove {
                aur_names.extend(names.clone());
                false
            } else {
                !names.is_empty()
            }
        })
        .collect();
    if groups.is_empty() && prefix.is_empty() && !aur_names.is_empty() {
        crate::aur_review::review(ctx, aur_names);
        return;
    }
    if groups.is_empty() && prefix.is_empty() {
        ctx.toast(&crate::i18n::gettext("No selected packages support this action."));
        return;
    }
    let count: usize = groups.iter().map(|(_, n)| n.len()).sum::<usize>() + prefix.len();
    let (backends, preview_groups) = (ctx.backends.clone(), groups.clone());
    let prefix_preview = prefix.iter().map(|c| c.display()).collect::<Vec<_>>().join("\n");
    let plan: Box<dyn FnOnce() -> TransactionPreview + Send> = Box::new(move || {
        let text = preview_groups
            .iter()
            .map(|(source, names)| {
                let backend = &backends[*source];
                let preview = match kind {
                    Kind::Install => backend.preview_install(names),
                    Kind::Remove => backend.preview_remove(names),
                    _ => TransactionPreview::default(),
                };
                format!("{}: {}\n{}", backend.name(), names.join(", "), preview.plan_text)
            })
            .collect::<Vec<_>>()
            .join("\n\n");
        TransactionPreview {
            available: true,
            plan_text: format!("{prefix_preview}\n{text}"),
        }
    });
    let (ctx2, backends) = (ctx.clone(), ctx.backends.clone());
    let title = format!("{} {count} selected packages", kind.verb());
    let title2 = title.clone();
    confirm(
        ctx,
        &title,
        &crate::i18n::gettext("Each source runs in order. Execution stops if any command fails."),
        kind.verb(),
        kind == Kind::Remove,
        Some(plan),
        move || {
            let ctx3 = ctx2.clone();
            run_commands_then(
                &ctx2,
                title2,
                "Batch completed".into(),
                move || {
                    prefix
                        .into_iter()
                        .chain(groups.iter().flat_map(|(source, names)| match kind {
                            Kind::Install => backends[*source].install_commands(names),
                            Kind::Remove => backends[*source].remove_commands(names),
                            Kind::Reinstall => backends[*source].reinstall_commands(names),
                            Kind::Update => backends[*source].upgrade_commands(names),
                        }))
                        .collect()
                },
                move || {
                    if !aur_names.is_empty() {
                        crate::aur_review::review_in_batch(&ctx3, aur_names, false);
                    }
                },
            );
        },
    );
}

pub fn custom(ctx: &Rc<Context>, title: &str, body: &str, commands: Vec<Command>) {
    let (ctx2, title2) = (ctx.clone(), title.to_owned());
    confirm(ctx, title, body, &crate::i18n::gettext("Continue"), false, None, move || {
        run_commands(&ctx2, title2, "Completed".into(), move || commands)
    });
}

pub fn transaction_item(ctx: &Rc<Context>, item: crate::context::Item, kind: Kind) {
    if ctx.backends[item.source].key() == "flatpak" && (item.package.repository.contains(':') || kind == Kind::Install && !item.package.repository.is_empty()) {
        let action = match kind {
            Kind::Install => "install",
            Kind::Remove => "remove",
            Kind::Update => "update",
            Kind::Reinstall => "reinstall",
        };
        let Some(commands) = athena_core::source_tools::flatpak_commands(action, &item.package.name, &item.package.repository, &item.package.install_ref)
        else {
            ctx.toast(&crate::i18n::gettext("Invalid Flatpak installation source."));
            return;
        };
        let title = format!("{} {}?", kind.verb(), item.package.name);
        let (ctx2, title2) = (ctx.clone(), title.clone());
        confirm(
            ctx,
            &title,
            &format!("Source: {}", item.package.repository),
            kind.verb(),
            kind == Kind::Remove,
            None,
            move || run_commands(&ctx2, title2, "Completed".into(), move || commands),
        );
    } else {
        transaction(ctx, item.source, kind, vec![item.package.name]);
    }
}
