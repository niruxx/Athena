//! The window commands run in: each command line, its live output, a
//! progress bar read from that output, and a clear result.

use std::io::Read;
use std::process::Stdio;
use std::rc::Rc;
use std::thread;

use adw::prelude::*;
use athena_core::Command;
use athena_core::progress;
use gtk::glib;

enum Event {
    Started(String),
    Output(String),
    Finished(bool),
}

/// Reads a pipe in chunks, sending complete lines (tools redraw progress
/// with carriage returns, so those end lines too).
fn forward_lines(mut pipe: impl Read, tx: async_channel::Sender<Event>) {
    let mut pending = Vec::new();
    let mut buf = [0u8; 4096];
    while let Ok(n) = pipe.read(&mut buf) {
        if n == 0 {
            break;
        }
        for &b in &buf[..n] {
            if b == b'\n' || b == b'\r' {
                let line = String::from_utf8_lossy(&pending).into_owned();
                pending.clear();
                let _ = tx.send_blocking(Event::Output(line));
            } else {
                pending.push(b);
            }
        }
    }
    if !pending.is_empty() {
        let _ = tx.send_blocking(Event::Output(String::from_utf8_lossy(&pending).into_owned()));
    }
}

/// Runs the commands in order on a worker thread, stopping at the first
/// failure.
fn run_all(commands: Vec<Command>, tx: async_channel::Sender<Event>) {
    for command in commands {
        let _ = tx.send_blocking(Event::Started(command.display()));
        let mut cmd = athena_core::process::std_command(&command);
        cmd.stdin(Stdio::null()).stdout(Stdio::piped()).stderr(Stdio::piped());
        let Ok(mut child) = cmd.spawn() else {
            let _ = tx.send_blocking(Event::Output(format!("(couldn't start {})", command.program)));
            let _ = tx.send_blocking(Event::Finished(false));
            return;
        };
        let out = child.stdout.take().map(|p| {
            let tx = tx.clone();
            thread::spawn(move || forward_lines(p, tx))
        });
        let err = child.stderr.take().map(|p| {
            let tx = tx.clone();
            thread::spawn(move || forward_lines(p, tx))
        });
        let status = child.wait();
        for reader in [out, err].into_iter().flatten() {
            let _ = reader.join();
        }
        let ok = status.as_ref().is_ok_and(|s| s.success());
        if !ok {
            let code = status.ok().and_then(|s| s.code()).unwrap_or(-1);
            let _ = tx.send_blocking(Event::Output(format!("(exited with code {code})")));
            let _ = tx.send_blocking(Event::Finished(false));
            return;
        }
    }
    let _ = tx.send_blocking(Event::Finished(true));
}

/// Shows the terminal window over `parent`, runs `commands`, and calls
/// `done` with whether they all succeeded once the user closes it (or
/// right away on success, with `close_on_success`).
pub fn run(parent: &impl IsA<gtk::Widget>, title: &str, commands: Vec<Command>, close_on_success: bool, done: impl FnOnce(bool) + 'static) {
    run_in_batch(parent, title, commands, close_on_success, true, done);
}

pub fn run_in_batch(
    parent: &impl IsA<gtk::Widget>,
    title: &str,
    mut commands: Vec<Command>,
    close_on_success: bool,
    snapshot: bool,
    done: impl FnOnce(bool) + 'static,
) {
    if snapshot && crate::settings::boolean("snapshot-before-transactions") && athena_core::snapshots::is_transaction(&commands) {
        match athena_core::snapshots::Provider::detect() {
            Some(provider) => commands.insert(0, provider.create(&format!("Athena: {title}"))),
            None => {
                let alert = adw::AlertDialog::new(
                    Some(&crate::i18n::gettext("Snapshot Provider Required")),
                    Some("Install and configure Snapper or Timeshift, or disable snapshots before transactions in Preferences. No transaction was started."),
                );
                alert.add_response("close", &crate::i18n::gettext("Close"));
                alert.present(Some(parent));
                done(false);
                return;
            }
        }
    }
    let close_on_success = close_on_success || crate::settings::boolean("close-on-success");
    let dialog = adw::Dialog::builder()
        .title(title)
        .content_width(680)
        .content_height(480)
        .can_close(false)
        .build();

    let status = gtk::Label::builder().label(crate::i18n::gettext("Running…")).xalign(0.0).wrap(true).build();
    status.add_css_class("heading");
    let progress = gtk::ProgressBar::new();
    let step = gtk::Label::builder().xalign(0.0).ellipsize(gtk::pango::EllipsizeMode::End).build();
    step.add_css_class("dim-label");

    let buffer = gtk::TextBuffer::new(None);
    let log = gtk::TextView::builder()
        .buffer(&buffer)
        .editable(false)
        .cursor_visible(false)
        .monospace(true)
        .wrap_mode(gtk::WrapMode::WordChar)
        .top_margin(8)
        .bottom_margin(8)
        .left_margin(10)
        .right_margin(10)
        .build();
    let scroller = gtk::ScrolledWindow::builder().child(&log).vexpand(true).build();
    let frame = gtk::Frame::builder().child(&scroller).build();
    let end_mark = buffer.create_mark(None, &buffer.end_iter(), false);

    let close = gtk::Button::builder()
        .label(crate::i18n::gettext("Close"))
        .sensitive(false)
        .halign(gtk::Align::End)
        .build();
    close.add_css_class("pill");

    let body = gtk::Box::builder()
        .orientation(gtk::Orientation::Vertical)
        .spacing(10)
        .margin_top(6)
        .margin_bottom(18)
        .margin_start(18)
        .margin_end(18)
        .build();
    body.append(&status);
    body.append(&progress);
    body.append(&step);
    body.append(&frame);
    body.append(&close);

    let toolbar = adw::ToolbarView::new();
    toolbar.add_top_bar(&adw::HeaderBar::builder().show_end_title_buttons(false).build());
    toolbar.set_content(Some(&body));
    dialog.set_child(Some(&toolbar));
    dialog.present(Some(parent));

    let (tx, rx) = async_channel::unbounded();
    let count = commands.len();
    thread::spawn(move || run_all(commands, tx));

    // However it's closed (the button, Escape), once it's finished.
    let done = Rc::new(std::cell::RefCell::new(Some(done)));
    let result = Rc::new(std::cell::Cell::new(false));
    {
        let (done, result) = (done.clone(), result.clone());
        dialog.connect_closed(move |_| {
            if let Some(done) = done.borrow_mut().take() {
                done(result.get());
            }
        });
    }
    {
        let dialog = dialog.clone();
        close.connect_clicked(move |_| {
            dialog.close();
        });
    }

    glib::spawn_future_local(async move {
        let mut index = 0;
        let mut output = String::new();
        let pulse = glib::timeout_add_local(std::time::Duration::from_millis(120), {
            let progress = progress.clone();
            // Indeterminate until the output says how far along it is.
            move || {
                if progress.has_css_class("pulsing") {
                    progress.pulse();
                }
                glib::ControlFlow::Continue
            }
        });
        let append = |text: &str| {
            buffer.insert(&mut buffer.end_iter(), text);
            log.scroll_mark_onscreen(&end_mark);
        };
        while let Ok(event) = rx.recv().await {
            match event {
                Event::Started(line) => {
                    index += 1;
                    if index > 1 {
                        append("\n");
                    }
                    append(&format!("$ {line}\n"));
                    output.push_str(&format!("$ {line}\n"));
                    progress.add_css_class("pulsing");
                    let program = line
                        .split_whitespace()
                        .find(|w| !w.starts_with('-') && *w != "pkexec" && *w != "env" && !w.contains('='))
                        .unwrap_or("")
                        .to_owned();
                    step.set_label(&if count > 1 {
                        format!("Step {index} of {count}: running {program}…")
                    } else {
                        format!("Running {program}…")
                    });
                }
                Event::Output(line) => {
                    output.push_str(&line);
                    output.push('\n');
                    if let Some(p) = progress::parse_line(&line) {
                        step.set_label(&p.label);
                        if let Some(fraction) = p.fraction {
                            progress.remove_css_class("pulsing");
                            progress.set_fraction(fraction);
                        }
                    }
                    if !progress::is_status_line(&line) {
                        append(&format!("{line}\n"));
                    }
                }
                Event::Finished(ok) => {
                    crate::settings::log(&output, !ok);
                    result.set(ok);
                    pulse.remove();
                    progress.set_visible(false);
                    if ok {
                        status.set_label(&crate::i18n::gettext("Completed successfully"));
                        status.add_css_class("success");
                        step.set_visible(false);
                    } else {
                        status.set_label(progress::explain_failure(&output).unwrap_or("Failed — see the output below"));
                        status.add_css_class("error");
                    }
                    dialog.set_can_close(true);
                    close.set_sensitive(true);
                    close.add_css_class("suggested-action");
                    close.grab_focus();
                    if ok && close_on_success {
                        close.emit_clicked();
                    }
                    break;
                }
            }
        }
    });
}
