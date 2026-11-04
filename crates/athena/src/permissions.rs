//! User Flatpak overrides; all blocking calls run off the GTK thread.
use std::rc::Rc;
use adw::prelude::*;
use athena_core::{OperationResult, backends::FlatpakBackend as Flatpak};
use crate::{context::Context, util};

pub fn show(ctx: &Rc<Context>, app: &str) {
    let view = adw::ToolbarView::new(); view.add_top_bar(&adw::HeaderBar::new());
    let page = adw::NavigationPage::builder().title(format!("Permissions: {app}")).child(&view).build();
    ctx.navigation.push(&page);
    load(ctx, app, &view);
}

fn apply(ctx: &Rc<Context>, work: impl FnOnce() -> OperationResult + Send + 'static, done: impl FnOnce(bool) + 'static) {
    let ctx = ctx.clone();
    util::background(work, move |result| {
        if !result.success { ctx.toast(&format!("Could not change permissions: {}", result.output)); }
        done(result.success);
    });
}

fn load(ctx: &Rc<Context>, app: &str, view: &adw::ToolbarView) {
    view.set_content(Some(&util::loading_page("Loading permissions…")));
    let (id, app, ctx, view) = (app.to_owned(), app.to_owned(), ctx.clone(), view.clone());
    util::background(move || Flatpak::permissions(&id), move |permissions| {
        let body = adw::PreferencesPage::new();
        let group = adw::PreferencesGroup::builder().title("Sandbox access").description("Changes are saved as user overrides. Restart the application to apply them.").build();
        type Setter = fn(&str, &str, bool) -> OperationResult;
        for (title, explanation, active, setter) in [
            ("Network", "Access the internet and local network", permissions.shared.contains("network"), Flatpak::set_shared as Setter),
            ("IPC", "Share interprocess communication with the host", permissions.shared.contains("ipc"), Flatpak::set_shared),
            ("Wayland", "Display windows using Wayland", permissions.sockets.contains("wayland"), Flatpak::set_socket),
            ("X11", "Access the X server, including other X11 windows", permissions.sockets.contains("x11"), Flatpak::set_socket),
            ("Audio", "Play and record audio", permissions.sockets.contains("pulseaudio"), Flatpak::set_socket),
            ("Session bus", "Unrestricted session D-Bus access", permissions.sockets.contains("session-bus"), Flatpak::set_socket),
            ("System bus", "Unrestricted system D-Bus access", permissions.sockets.contains("system-bus"), Flatpak::set_socket),
            ("SSH agent", "Access keys through the SSH authentication agent", permissions.sockets.contains("ssh-auth"), Flatpak::set_socket),
            ("GPU", "Access graphics acceleration devices", permissions.devices.contains("dri"), Flatpak::set_device),
            ("All devices", "Access all host devices", permissions.devices.contains("all"), Flatpak::set_device),
            ("Development", "Permit debugging and tracing", permissions.features.contains("devel"), Flatpak::set_feature),
        ] {
            let key = match title { "Network" => "network", "IPC" => "ipc", "Wayland" => "wayland", "X11" => "x11", "Audio" => "pulseaudio", "Session bus" => "session-bus", "System bus" => "system-bus", "SSH agent" => "ssh-auth", "GPU" => "dri", "All devices" => "all", _ => "devel" };
            let row = adw::SwitchRow::builder().title(title).subtitle(explanation).active(active).build();
            let (app, ctx) = (app.clone(), ctx.clone());
            row.connect_active_notify(move |row| {
                let wanted = row.is_active(); row.set_sensitive(false);
                let (app, weak) = (app.clone(), row.downgrade());
                apply(&ctx, move || setter(&app, key, wanted), move |ok| {
                    if let Some(row) = weak.upgrade() {
                        // Keep failed writes visibly distinct and reload before retrying.
                        row.set_sensitive(ok);
                        if !ok { row.set_subtitle("Change failed. Reopen permissions to retry."); }
                    }
                });
            });
            group.add(&row);
        }
        body.add(&group);
        let resources = adw::PreferencesGroup::builder().title("Files, D-Bus and environment").build();
        let entries: Vec<(String, String)> = permissions.filesystems.into_iter().map(|v| ("filesystem".into(), v))
            .chain(permissions.session_bus_talk.into_iter().map(|v| ("session talk".into(), v)))
            .chain(permissions.session_bus_own.into_iter().map(|v| ("session own".into(), v)))
            .chain(permissions.system_bus_talk.into_iter().map(|v| ("system talk".into(), v)))
            .chain(permissions.system_bus_own.into_iter().map(|v| ("system own".into(), v)))
            .chain(permissions.env_vars.into_iter().map(|(k,v)| ("environment".into(), format!("{k}={v}")))).collect();
        for (kind, value) in entries {
            let row = adw::ActionRow::builder().title(gtk::glib::markup_escape_text(&value)).subtitle(&kind).build();
            let remove = gtk::Button::builder().icon_name("list-remove-symbolic").tooltip_text("Revoke permission").valign(gtk::Align::Center).build();
            let (ctx, app, view) = (ctx.clone(), app.clone(), view.clone());
            remove.connect_clicked(move |_| {
                let (id, kind, value) = (app.clone(), kind.clone(), value.clone());
                let (ctx2, app, view) = (ctx.clone(), app.clone(), view.clone());
                apply(&ctx, move || match kind.as_str() {
                    "filesystem" => Flatpak::remove_filesystem(&id, &value),
                    "environment" => Flatpak::unset_env(&id, value.split('=').next().unwrap_or(&value)),
                    _ => { let (bus, mode) = kind.split_once(' ').unwrap(); Flatpak::revoke_dbus_name(&id, bus, mode, &value) }
                }, move |ok| { if ok { load(&ctx2, &app, &view); } });
            });
            row.add_suffix(&remove); resources.add(&row);
        }
        let kind = adw::ComboRow::builder().title("Permission type").model(&gtk::StringList::new(&["Folder", "Session bus talk", "Session bus own", "System bus talk", "System bus own", "Environment KEY=value"])).build();
        let value = adw::EntryRow::builder().title("Value (folder may end in :ro)").build();
        let add = gtk::Button::with_label("Add Permission");
        resources.add(&kind); resources.add(&value); resources.add(&add);
        {
            let (ctx, app, view) = (ctx.clone(), app.clone(), view.clone());
            add.connect_clicked(move |_| {
                let text = value.text().trim().to_owned(); let selected = kind.selected();
                if text.is_empty() { ctx.toast("Enter a permission value."); return; }
                if selected == 5 && !text.contains('=') { ctx.toast("Use KEY=value for an environment variable."); return; }
                let (id, ctx2, app, view) = (app.clone(), ctx.clone(), app.clone(), view.clone());
                apply(&ctx, move || match selected {
                    0 => Flatpak::add_filesystem(&id, &text),
                    5 => { let (k,v) = text.split_once('=').unwrap(); Flatpak::set_env(&id, k, v) },
                    _ => Flatpak::grant_dbus_name(&id, if selected >= 3 { "system" } else { "session" }, if selected == 2 || selected == 4 { "own" } else { "talk" }, &text),
                }, move |ok| { if ok { load(&ctx2, &app, &view); } });
            });
        }
        let reset = gtk::Button::with_label("Reset User Overrides…");
        reset.add_css_class("destructive-action"); resources.add(&reset);
        reset.connect_clicked(move |_| {
            let dialog = adw::AlertDialog::new(Some("Reset user overrides?"), Some("The app's original permissions and any system overrides will apply."));
            dialog.add_responses(&[("cancel", "Cancel"), ("reset", "Reset")]); dialog.set_close_response("cancel");
            let (id, ctx2, app, view2) = (app.clone(), ctx.clone(), app.clone(), view.clone());
            let ctx3 = ctx.clone();
            dialog.connect_response(Some("reset"), move |_, _| {
                let (id, ctx2, app, view) = (id.clone(), ctx2.clone(), app.clone(), view2.clone());
                apply(&ctx3, move || Flatpak::reset_overrides(&id), move |ok| { if ok { load(&ctx2, &app, &view); } });
            });
            dialog.present(Some(&ctx.window));
        });
        body.add(&resources); view.set_content(Some(&body));
    });
}
