//! Build review is mandatory for every AUR installation and update.
use crate::{context::Context, terminal, util};
use adw::prelude::*;
use athena_core::aur::{self, Plan};
use std::{
    cell::{Cell, RefCell},
    rc::Rc,
};

pub fn review(ctx: &Rc<Context>, names: Vec<String>) {
    review_in_batch(ctx, names, true);
}
pub fn review_in_batch(ctx: &Rc<Context>, names: Vec<String>, snapshot: bool) {
    if !athena_core::process::executable_exists("git") || !athena_core::process::executable_exists("makepkg") {
        let dialog = adw::AlertDialog::new(
            Some(&crate::i18n::gettext("AUR Build Tools Required")),
            Some("Install base-devel and git, then retry the AUR transaction."),
        );
        dialog.add_responses(&[
            ("cancel", &crate::i18n::gettext("Cancel")),
            ("install", &crate::i18n::gettext("Install Build Tools…")),
        ]);
        dialog.set_close_response("cancel");
        let ctx2 = ctx.clone();
        dialog.connect_response(Some("install"), move |_, _| {
            if let Some(source) = ctx2.backends.iter().position(|b| b.key() == "pacman") {
                crate::actions::transaction(&ctx2, source, crate::actions::Kind::Install, vec!["base-devel".into(), "git".into()]);
            }
        });
        dialog.present(Some(&ctx.window));
        return;
    }
    let dialog = adw::Dialog::builder()
        .title(crate::i18n::gettext("Review AUR Build Files"))
        .content_width(900)
        .content_height(700)
        .build();
    let toolbar = adw::ToolbarView::new();
    toolbar.add_top_bar(&adw::HeaderBar::new());
    toolbar.set_content(Some(&util::loading_page("Fetching build files and resolving AUR dependencies…")));
    dialog.set_child(Some(&toolbar));
    dialog.present(Some(&ctx.window));
    let (ctx, weak) = (ctx.clone(), dialog.downgrade());
    util::background(
        move || aur::prepare(&names),
        move |result| {
            let Some(dialog) = weak.upgrade().filter(|d| d.is_visible()) else {
                return;
            };
            match result {
                Ok(plan) => show(&ctx, &dialog, &toolbar, plan, snapshot),
                Err(error) => toolbar.set_content(Some(
                    &adw::StatusPage::builder()
                        .title(crate::i18n::gettext("Cannot Prepare AUR Transaction"))
                        .description(&error)
                        .build(),
                )),
            }
        },
    );
}
fn show(ctx: &Rc<Context>, dialog: &adw::Dialog, toolbar: &adw::ToolbarView, plan: Plan, snapshot: bool) {
    let plan = Rc::new(RefCell::new(plan));
    let positions = plan
        .borrow()
        .units
        .iter()
        .enumerate()
        .flat_map(|(unit, u)| u.files.iter().enumerate().map(move |(file, _)| (unit, file)))
        .collect::<Vec<_>>();
    let labels = positions
        .iter()
        .map(|(u, f)| {
            let plan = plan.borrow();
            format!(
                "{} / {}{}",
                plan.units[*u].base,
                plan.units[*u].files[*f].name,
                if plan.units[*u].as_dependencies { " (dependency)" } else { "" }
            )
        })
        .collect::<Vec<_>>();
    let picker = gtk::DropDown::from_strings(&labels.iter().map(String::as_str).collect::<Vec<_>>());
    let body = gtk::Box::builder()
        .orientation(gtk::Orientation::Vertical)
        .spacing(8)
        .margin_start(12)
        .margin_end(12)
        .margin_bottom(12)
        .build();
    body.append(&gtk::Label::builder().label(crate::i18n::gettext("Review every file, including dependencies. Edits are saved to this transaction's private copy. Build scripts run as your user; native dependencies and installation use polkit.")).wrap(true).xalign(0.0).build());
    body.append(&picker);
    let editor = gtk::TextView::builder().monospace(true).wrap_mode(gtk::WrapMode::None).vexpand(true).build();
    body.append(&gtk::ScrolledWindow::builder().child(&editor).vexpand(true).build());
    let risks = gtk::Label::builder().wrap(true).xalign(0.0).selectable(true).build();
    risks.add_css_class("warning");
    body.append(&risks);
    let changes = gtk::TextView::builder()
        .editable(false)
        .cursor_visible(false)
        .monospace(true)
        .wrap_mode(gtk::WrapMode::WordChar)
        .build();
    let expander = gtk::Expander::builder()
        .label(crate::i18n::gettext("Changes from the last approved copy"))
        .expanded(true)
        .child(
            &gtk::ScrolledWindow::builder()
                .child(&changes)
                .min_content_height(100)
                .max_content_height(160)
                .build(),
        )
        .build();
    body.append(&expander);
    let count = gtk::Label::new(None);
    body.append(&count);
    let mark = gtk::Button::with_label(&crate::i18n::gettext("Mark This File Reviewed"));
    body.append(&mark);
    let approve = gtk::Button::builder()
        .label(crate::i18n::gettext("Approve Reviewed Copies and Build"))
        .sensitive(false)
        .build();
    approve.add_css_class("suggested-action");
    body.append(&approve);
    let reviewed = Rc::new(RefCell::new(vec![false; positions.len()]));
    let active = Rc::new(Cell::new(0usize));
    let buffers = positions
        .iter()
        .map(|(u, f)| {
            let buffer = gtk::TextBuffer::new(None);
            buffer.set_text(&plan.borrow().units[*u].files[*f].text);
            buffer
        })
        .collect::<Vec<_>>();
    let update: Rc<dyn Fn()> = {
        let (reviewed, count, approve) = (reviewed.clone(), count.downgrade(), approve.downgrade());
        Rc::new(move || {
            let (Some(count), Some(approve)) = (count.upgrade(), approve.upgrade()) else {
                return;
            };
            let reviewed = reviewed.borrow();
            let total = reviewed.iter().filter(|v| **v).count();
            count.set_label(&format!("{total} of {} files reviewed", reviewed.len()));
            approve.set_sensitive(total == reviewed.len() && !reviewed.is_empty());
        })
    };
    let display: Rc<dyn Fn(usize)> = {
        let (buffers, editor, risks, changes, positions, plan) = (
            buffers.iter().map(|buffer| buffer.downgrade()).collect::<Vec<_>>(),
            editor.downgrade(),
            risks.downgrade(),
            changes.downgrade(),
            positions.clone(),
            plan.clone(),
        );
        Rc::new(move |index| {
            let (Some(editor), Some(risks), Some(changes), Some(buffer)) = (editor.upgrade(), risks.upgrade(), changes.upgrade(), buffers[index].upgrade())
            else {
                return;
            };
            editor.set_buffer(Some(&buffer));
            let text = buffer.text(&buffer.start_iter(), &buffer.end_iter(), true).to_string();
            let risky = aur::risky_lines(&text);
            risks.set_label(&if risky.is_empty() {
                "No known risk patterns found. This does not establish that the script is safe.".into()
            } else {
                format!("Review these lines carefully:\n{}", risky.join("\n"))
            });
            let (u, f) = positions[index];
            let previous = plan.borrow().units[u].files[f].previous.clone();
            changes.buffer().set_text(
                &previous
                    .map(|previous| aur::diff(&previous, &text))
                    .unwrap_or_else(|| "No previously approved copy. Review the complete file above.".into()),
            );
        })
    };
    for (index, buffer) in buffers.iter().enumerate() {
        let (reviewed, update, display, active) = (reviewed.clone(), update.clone(), display.clone(), active.clone());
        buffer.connect_changed(move |_| {
            reviewed.borrow_mut()[index] = false;
            update();
            if active.get() == index {
                display(index);
            }
        });
    }
    {
        let (active, display) = (active.clone(), display.clone());
        picker.connect_selected_notify(move |picker| {
            let index = picker.selected() as usize;
            active.set(index);
            display(index);
        });
    }
    {
        let (reviewed, update, picker) = (reviewed.clone(), update.clone(), picker.clone());
        mark.connect_clicked(move |_| {
            let index = active.get();
            reviewed.borrow_mut()[index] = true;
            update();
            if let Some(next) = reviewed.borrow().iter().position(|v| !*v) {
                picker.set_selected(next as u32);
            }
        });
    }
    display(0);
    update();
    let (ctx, dialog) = (ctx.clone(), dialog.downgrade());
    approve.connect_clicked(move |button| {
        if reviewed.borrow().iter().any(|v| !*v) {
            return;
        }
        let Some(dialog) = dialog.upgrade() else {
            return;
        };
        button.set_sensitive(false);
        editor.set_sensitive(false);
        picker.set_sensitive(false);
        mark.set_sensitive(false);
        dialog.set_can_close(false);
        for (index, (u, f)) in positions.iter().enumerate() {
            plan.borrow_mut().units[*u].files[*f].text = buffers[index].text(&buffers[index].start_iter(), &buffers[index].end_iter(), true).to_string();
        }
        let mut approved = plan.borrow().clone();
        let (ctx2, dialog, button, editor, picker, mark) = (ctx.clone(), dialog.clone(), button.clone(), editor.clone(), picker.clone(), mark.clone());
        util::background(
            move || aur::approve(&mut approved),
            move |result| match result {
                Ok(path) => {
                    dialog.set_can_close(true);
                    dialog.close();
                    let Ok(executable) = std::env::current_exe() else {
                        ctx2.toast(&crate::i18n::gettext("Could not locate Athena's build runner."));
                        return;
                    };
                    let command = athena_core::Command::new(
                        &executable.to_string_lossy(),
                        ["--execute-reviewed-aur".to_owned(), path.to_string_lossy().into_owned()],
                    );
                    let ctx = ctx2.clone();
                    terminal::run_in_batch(
                        &ctx2.window,
                        "Build Reviewed AUR Packages",
                        vec![command],
                        crate::settings::boolean("close-on-success"),
                        snapshot,
                        move |ok| {
                            ctx.toast(if ok {
                                "AUR packages installed."
                            } else {
                                "AUR build stopped. Check the output."
                            });
                            ctx.packages_changed();
                        },
                    );
                }
                Err(error) => {
                    ctx2.toast(&error);
                    button.set_sensitive(true);
                    editor.set_sensitive(true);
                    picker.set_sensitive(true);
                    mark.set_sensitive(true);
                    dialog.set_can_close(true);
                }
            },
        );
    });
    toolbar.set_content(Some(&body));
}

#[cfg(debug_assertions)]
pub fn screenshot(ctx: &Rc<Context>) {
    let dialog = adw::Dialog::builder()
        .title(crate::i18n::gettext("Review AUR Build Files"))
        .content_width(900)
        .content_height(700)
        .build();
    let toolbar = adw::ToolbarView::new();
    toolbar.add_top_bar(&adw::HeaderBar::new());
    dialog.set_child(Some(&toolbar));
    dialog.present(Some(&ctx.window));
    let file = aur::BuildFile {
        name: "PKGBUILD".into(),
        text: "pkgname=example\npkgver=2\npkgrel=1\nsource=(https://example.org/source.tar.gz)\nsha256sums=('SKIP')\n\nbuild() {\n    make\n}\n".into(),
        previous: Some("pkgname=example\npkgver=1\npkgrel=1\n".into()),
        digest: String::new(),
    };
    show(
        ctx,
        &dialog,
        &toolbar,
        Plan {
            root: std::env::temp_dir().join("athena-non-buildable-preview"),
            units: vec![aur::Unit {
                base: "example".into(),
                packages: vec!["example".into()],
                as_dependencies: false,
                dependency_packages: Vec::new(),
                files: vec![file],
            }],
        },
        true,
    );
}
