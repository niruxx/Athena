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
    if crate::settings::boolean("skip-confirmations") { accepted(); return; }
    let dialog = adw::AlertDialog::new(Some(heading), Some(body));
    dialog.add_response("cancel", "Cancel");
    dialog.add_response("ok", verb);
    dialog.set_response_appearance("ok", if destructive { adw::ResponseAppearance::Destructive } else { adw::ResponseAppearance::Suggested });
    dialog.set_default_response(Some("ok"));
    dialog.set_close_response("cancel");

    if let Some(plan) = plan {
        let stack = gtk::Stack::new();
        let spinner = gtk::Spinner::builder().spinning(true).halign(gtk::Align::Center).margin_top(6).build();
        stack.add_named(&spinner, Some("loading"));
        let text = gtk::Label::builder().xalign(0.0).wrap(true).build();
        text.add_css_class("monospace");
        text.add_css_class("caption");
        let scroller = gtk::ScrolledWindow::builder().child(&text).max_content_height(260).propagate_natural_height(true).build();
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
        if response == "ok" {
            if let Some(accepted) = accepted.borrow_mut().take() {
                accepted();
            }
        }
    });
    dialog.present(Some(&ctx.window));
}

/// Runs commands computed off the main thread (Flatpak looks up each app's
/// remote, for instance) in the terminal window, then refreshes.
fn run_commands(ctx: &Rc<Context>, title: String, done_message: String, commands: impl FnOnce() -> Vec<Command> + Send + 'static) {
    let ctx = ctx.clone();
    util::background(commands, move |commands| {
        if commands.is_empty() {
            ctx.toast("Nothing to do.");
            return;
        }
        let ctx2 = ctx.clone();
        terminal::run(&ctx.window, &title, commands, crate::settings::boolean("close-on-success"), move |ok| {
            ctx2.toast(if ok { &done_message } else { "Something went wrong — nothing may have changed." });
            ctx2.packages_changed();
        });
    });
}

/// Install, remove, reinstall, or update `names` from one source. An update
/// with no names is the source's own full update (`pacman -Syu`…).
pub fn transaction(ctx: &Rc<Context>, source: usize, kind: Kind, names: Vec<String>) {
    if names.is_empty() && kind != Kind::Update {
        return;
    }
    let backend = ctx.backends[source].clone();
    let what = if names.is_empty() { format!("everything from {}", ctx.source_name(source)) } else { summarize(&names) };
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
    let backends: Vec<_> = counts.iter().filter(|(_, n)| *n > 0).map(|(s, _)| ctx.backends[*s].clone()).collect();
    let ctx2 = ctx.clone();
    confirm(
        ctx,
        &format!("Install {total} update{}?", if total == 1 { "" } else { "s" }),
        &format!("{breakdown}. You'll be asked for your password."),
        "Update",
        false,
        None,
        move || {
            run_commands(&ctx2, "Update everything".into(), "Updates installed".into(), move || {
                backends.iter().flat_map(|b| b.upgrade_commands(&[])).collect()
            });
        },
    );
}

/// Pick a destination before constructing any download commands.
pub fn download(ctx: &Rc<Context>, source: usize, names: Vec<String>) {
    if names.is_empty() { return; }
    let dialog = adw::AlertDialog::new(Some("Download packages"), Some("Include dependencies? Flatpak downloads into its own cache; other sources use the selected folder."));
    dialog.add_responses(&[("cancel", "Cancel"), ("only", "Packages only"), ("dependencies", "Include dependencies")]);
    dialog.set_close_response("cancel");
    let parent = ctx.window.clone();
    let ctx = ctx.clone();
    dialog.connect_response(None, move |_, response| {
        if response == "cancel" { return; }
        let dependencies = response == "dependencies";
        let (ctx, names) = (ctx.clone(), names.clone());
        let picker = gtk::FileDialog::builder().title("Download to folder").build();
        let parent = ctx.window.clone();
        picker.select_folder(Some(&parent), None::<&gtk::gio::Cancellable>, move |result| {
            let Ok(folder) = result else { return };
            let Some(path) = folder.path() else { ctx.toast("Choose a local folder."); return };
            let backend = ctx.backends[source].clone();
            run_commands(&ctx, "Download packages".into(), "Download complete".into(), move || backend.download_commands(&names, &path.to_string_lossy(), dependencies));
        });
    });
    dialog.present(Some(&parent));
}
