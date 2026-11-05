//! User Flatpak overrides; all blocking calls run off the GTK thread.
use crate::{context::Context, util};
use adw::prelude::*;
use athena_core::{OperationResult, backends::FlatpakBackend as Flatpak};
use std::rc::Rc;

pub fn editor(ctx: &Rc<Context>, app: &str) -> adw::ToolbarView {
    let view = adw::ToolbarView::new();
    load(ctx, app, &view);
    view
}

fn apply(ctx: &Rc<Context>, work: impl FnOnce() -> OperationResult + Send + 'static, done: impl FnOnce(bool) + 'static) {
    let ctx = ctx.clone();
    util::background(work, move |result| {
        if !result.success {
            ctx.toast(&format!("Could not change permissions: {}", result.output));
        }
        done(result.success);
    });
}

fn load(ctx: &Rc<Context>, app: &str, view: &adw::ToolbarView) {
    view.set_content(Some(&util::loading_page("Loading permissions…")));
    let (id, app, ctx, view) = (app.to_owned(), app.to_owned(), ctx.clone(), view.clone());
    util::background(
        move || Flatpak::try_permissions(&id),
        move |permissions| {
            let permissions = match permissions {
                Ok(permissions) => permissions,
                Err(error) => {
                    view.set_content(Some(
                        &adw::StatusPage::builder()
                            .title(crate::i18n::gettext("Could Not Read Permissions"))
                            .description(&error)
                            .build(),
                    ));
                    return;
                }
            };
            let body = adw::PreferencesPage::new();
            body.add_css_class("page-enter");
            let hero = gtk::Box::builder().spacing(18).build();
            hero.add_css_class("identity-card");
            hero.append(&crate::catalog_ui::app_icon(&app, 64));
            let text = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(6).valign(gtk::Align::Center).hexpand(true).build();
            text.append(&util::label(&crate::catalog_ui::app_name(&app), &["identity-title"]));
            text.append(&util::label(&app, &["dim-label", "caption", "monospace"]));
            let stats = gtk::FlowBox::builder().selection_mode(gtk::SelectionMode::None).column_spacing(8).row_spacing(6).max_children_per_line(6).build();
            let granted = permissions.shared.len() + permissions.sockets.len() + permissions.devices.len() + permissions.features.len();
            stats.append(&util::label(&format!("{granted} capabilities"), &["stat-pill"]));
            stats.append(&util::label(&format!("{} folders", permissions.filesystems.len()), &["stat-pill"]));
            text.append(&stats);
            hero.append(&text);
            let hero_group = adw::PreferencesGroup::builder()
                .description(crate::i18n::gettext("Changes are saved as user overrides. Restart the application to apply them."))
                .build();
            hero_group.add(&hero);
            body.add(&hero_group);
            let sections = [
                adw::PreferencesGroup::builder().title("Network").description("Internet and interprocess communication").build(),
                adw::PreferencesGroup::builder().title("Display, sound and services").description("Sockets the app can connect to").build(),
                adw::PreferencesGroup::builder().title("Devices").description("Hardware the app can open").build(),
                adw::PreferencesGroup::builder().title("Advanced").description("Low-level sandbox features").build(),
            ];
            type Setter = fn(&str, &str, bool) -> OperationResult;
            let definitions = [
                (0, "network", "Network", "Access the internet and local network"),
                (0, "ipc", "IPC", "Share host interprocess communication"),
                (1, "wayland", "Wayland", "Display windows using Wayland"),
                (1, "x11", "X11", "Access the X server, including other X11 windows"),
                (1, "fallback-x11", "X11 fallback", "Use X11 when Wayland is unavailable"),
                (1, "pulseaudio", "Audio", "Play and record audio"),
                (1, "session-bus", "Session bus", "Unrestricted session D-Bus access"),
                (1, "system-bus", "System bus", "Unrestricted system D-Bus access"),
                (1, "ssh-auth", "SSH agent", "Access the SSH authentication agent"),
                (1, "gpg-agent", "GPG agent", "Access the GPG agent"),
                (1, "pcsc", "Smart cards", "Access smart-card readers"),
                (1, "cups", "Printing", "Access the print server"),
                (2, "dri", "GPU", "Access graphics acceleration devices"),
                (2, "all", "All devices", "Access all host devices"),
                (2, "kvm", "Virtual machines", "Access hardware virtualization"),
                (2, "shm", "Shared memory devices", "Access shared-memory devices"),
                (3, "devel", "Development", "Permit debugging and tracing"),
                (3, "multiarch", "Multiple architectures", "Run binaries for other architectures"),
                (3, "bluetooth", "Bluetooth", "Access Bluetooth sockets"),
                (3, "canbus", "CAN bus", "Access CAN bus sockets"),
                (3, "per-app-dev-shm", "Private shared memory", "Use private application shared memory"),
            ];
            let mut switches = definitions
                .iter()
                .map(|(kind, key, title, explanation)| (*kind, (*key).to_owned(), (*title).to_owned(), (*explanation).to_owned()))
                .collect::<Vec<_>>();
            for (kind, values) in [
                (0, &permissions.shared),
                (1, &permissions.sockets),
                (2, &permissions.devices),
                (3, &permissions.features),
            ] {
                for value in values {
                    if !switches.iter().any(|(k, key, _, _)| *k == kind && key == value) {
                        switches.push((kind, value.clone(), value.clone(), "Permission reported by Flatpak".into()));
                    }
                }
            }
            for (kind, key, title, explanation) in switches {
                let (values, setter): (_, Setter) = match kind {
                    0 => (&permissions.shared, Flatpak::set_shared),
                    1 => (&permissions.sockets, Flatpak::set_socket),
                    2 => (&permissions.devices, Flatpak::set_device),
                    _ => (&permissions.features, Flatpak::set_feature),
                };
                let active = values.contains(&key);
                let row = adw::SwitchRow::builder().title(&title).subtitle(&explanation).active(active).build();
                let risky = matches!(key.as_str(), "x11" | "session-bus" | "system-bus" | "all" | "devel" | "kvm");
                let icon = gtk::Image::builder().icon_name(permission_icon(kind, &key)).valign(gtk::Align::Center).build();
                icon.add_css_class("permission-icon");
                if risky {
                    icon.add_css_class("warning");
                    icon.set_tooltip_text(Some("Broad access: grants a lot outside the sandbox"));
                }
                row.add_prefix(&icon);
                let (app, ctx) = (app.clone(), ctx.clone());
                let changing = Rc::new(std::cell::Cell::new(false));
                row.connect_active_notify(move |row| {
                    if changing.get() {
                        return;
                    }
                    let changing = changing.clone();
                    let wanted = row.is_active();
                    row.set_sensitive(false);
                    let (app, weak, key) = (app.clone(), row.downgrade(), key.clone());
                    apply(
                        &ctx,
                        move || setter(&app, &key, wanted),
                        move |ok| {
                            if let Some(row) = weak.upgrade() {
                                if !ok {
                                    changing.set(true);
                                    row.set_active(!wanted);
                                    changing.set(false);
                                }
                                row.set_sensitive(true);
                            }
                        },
                    );
                });
                sections[kind as usize].add(&row);
            }
            for section in &sections {
                body.add(section);
            }
            let resources = adw::PreferencesGroup::builder()
                .title(crate::i18n::gettext("Files, D-Bus and environment"))
                .description("Granted folders, bus names and variables. Remove one to revoke it.")
                .build();
            let entries: Vec<(String, String)> = permissions
                .filesystems
                .into_iter()
                .map(|v| ("filesystem".into(), v))
                .chain(permissions.session_bus_talk.into_iter().map(|v| ("session talk".into(), v)))
                .chain(permissions.session_bus_own.into_iter().map(|v| ("session own".into(), v)))
                .chain(permissions.system_bus_talk.into_iter().map(|v| ("system talk".into(), v)))
                .chain(permissions.system_bus_own.into_iter().map(|v| ("system own".into(), v)))
                .chain(permissions.env_vars.into_iter().map(|(k, v)| ("environment".into(), format!("{k}={v}"))))
                .collect();
            let has_resources = !entries.is_empty();
            for (kind, value) in entries {
                let row = adw::ActionRow::builder().title(gtk::glib::markup_escape_text(&value)).subtitle(&kind).build();
                let icon = gtk::Image::builder()
                    .icon_name(match kind.as_str() { "filesystem" => "folder-symbolic", "environment" => "utilities-terminal-symbolic", _ => "network-workgroup-symbolic" })
                    .valign(gtk::Align::Center)
                    .build();
                icon.add_css_class("permission-icon");
                if kind == "filesystem" && matches!(value.trim_end_matches(":ro"), "host" | "home" | "host-os" | "host-etc") {
                    icon.add_css_class("warning");
                }
                row.add_prefix(&icon);
                let remove = gtk::Button::builder()
                    .icon_name("list-remove-symbolic")
                    .tooltip_text(crate::i18n::gettext("Revoke permission"))
                    .valign(gtk::Align::Center)
                    .build();
                remove.add_css_class("flat");
                remove.add_css_class("error");
                let (ctx, app, view) = (ctx.clone(), app.clone(), view.clone());
                remove.connect_clicked(move |_| {
                    let (id, kind, value) = (app.clone(), kind.clone(), value.clone());
                    let (ctx2, app, view) = (ctx.clone(), app.clone(), view.clone());
                    apply(
                        &ctx,
                        move || match kind.as_str() {
                            "filesystem" => Flatpak::remove_filesystem(&id, &value),
                            "environment" => Flatpak::unset_env(&id, value.split('=').next().unwrap_or(&value)),
                            _ => {
                                let (bus, mode) = kind.split_once(' ').unwrap();
                                Flatpak::revoke_dbus_name(&id, bus, mode, &value)
                            }
                        },
                        move |ok| {
                            if ok {
                                load(&ctx2, &app, &view);
                            }
                        },
                    );
                });
                row.add_suffix(&remove);
                resources.add(&row);
            }
            let kind = adw::ComboRow::builder()
                .title(crate::i18n::gettext("Permission type"))
                .model(&gtk::StringList::new(&[
                    "Folder",
                    "Session bus talk",
                    "Session bus own",
                    "System bus talk",
                    "System bus own",
                    "Environment KEY=value",
                ]))
                .build();
            let value = adw::EntryRow::builder().title(crate::i18n::gettext("Value (folder may end in :ro)")).build();
            let add = gtk::Button::builder().icon_name("list-add-symbolic").tooltip_text(crate::i18n::gettext("Add Permission")).valign(gtk::Align::Center).build();
            add.add_css_class("suggested-action");
            add.add_css_class("circular");
            value.add_suffix(&add);
            let grant = adw::PreferencesGroup::builder().title("Grant access").description("Give the app an extra folder, bus name or environment variable.").build();
            grant.add(&kind);
            grant.add(&value);
            {
                let (ctx, app, view) = (ctx.clone(), app.clone(), view.clone());
                add.connect_clicked(move |_| {
                    let text = value.text().trim().to_owned();
                    let selected = kind.selected();
                    if text.is_empty() {
                        ctx.toast(&crate::i18n::gettext("Enter a permission value."));
                        return;
                    }
                    if selected == 5 && !text.contains('=') {
                        ctx.toast(&crate::i18n::gettext("Use KEY=value for an environment variable."));
                        return;
                    }
                    let (id, ctx2, app, view) = (app.clone(), ctx.clone(), app.clone(), view.clone());
                    apply(
                        &ctx,
                        move || match selected {
                            0 => Flatpak::add_filesystem(&id, &text),
                            5 => {
                                let (k, v) = text.split_once('=').unwrap();
                                Flatpak::set_env(&id, k, v)
                            }
                            _ => Flatpak::grant_dbus_name(
                                &id,
                                if selected >= 3 { "system" } else { "session" },
                                if selected == 2 || selected == 4 { "own" } else { "talk" },
                                &text,
                            ),
                        },
                        move |ok| {
                            if ok {
                                load(&ctx2, &app, &view);
                            }
                        },
                    );
                });
            }
            let reset = gtk::Button::builder().label(crate::i18n::gettext("Reset…")).valign(gtk::Align::Center).build();
            reset.add_css_class("destructive-action");
            reset.add_css_class("row-action");
            let reset_row = adw::ActionRow::builder().title("Reset user overrides").subtitle("Return to the permissions the app shipped with.").build();
            reset_row.add_suffix(&reset);
            let reset_group = adw::PreferencesGroup::new();
            reset_group.add(&util::with_icon(reset_row, "edit-undo-symbolic"));
            let reset_view = view.clone();
            reset.connect_clicked(move |_| {
                let dialog = adw::AlertDialog::new(
                    Some(&crate::i18n::gettext("Reset user overrides?")),
                    Some("The app's original permissions and any system overrides will apply."),
                );
                dialog.add_responses(&[("cancel", &crate::i18n::gettext("Cancel")), ("reset", &crate::i18n::gettext("Reset"))]);
                dialog.set_close_response("cancel");
                let (id, ctx2, app, view2) = (app.clone(), ctx.clone(), app.clone(), reset_view.clone());
                let ctx3 = ctx.clone();
                dialog.connect_response(Some("reset"), move |_, _| {
                    let (id, ctx2, app, view) = (id.clone(), ctx2.clone(), app.clone(), view2.clone());
                    apply(
                        &ctx3,
                        move || Flatpak::reset_overrides(&id),
                        move |ok| {
                            if ok {
                                load(&ctx2, &app, &view);
                            }
                        },
                    );
                });
                dialog.present(Some(&ctx.window));
            });
            if has_resources {
                body.add(&resources);
            }
            body.add(&grant);
            body.add(&reset_group);
            view.set_content(Some(&body));
        },
    );
}

fn permission_icon(kind: i32, key: &str) -> &'static str {
    match (kind, key) {
        (0, "network") => "network-wireless-symbolic",
        (0, _) => "network-workgroup-symbolic",
        (1, "wayland" | "x11" | "fallback-x11") => "video-display-symbolic",
        (1, "pulseaudio") => "audio-speakers-symbolic",
        (1, "ssh-auth" | "gpg-agent") => "dialog-password-symbolic",
        (1, "pcsc") => "auth-smartcard-symbolic",
        (1, "cups") => "printer-symbolic",
        (1, _) => "preferences-system-symbolic",
        (2, "dri") => "video-display-symbolic",
        (2, "kvm") => "computer-symbolic",
        (2, _) => "drive-harddisk-symbolic",
        (_, "bluetooth") => "bluetooth-symbolic",
        (_, "devel") => "utilities-terminal-symbolic",
        _ => "applications-system-symbolic",
    }
}
