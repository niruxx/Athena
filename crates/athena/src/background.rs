//! Opt-in, read-only update checks. Background lifetime requires portal consent.
use crate::i18n::gettext;
use adw::prelude::*;
use gtk::{gio, glib};
use std::{
    cell::{Cell, RefCell},
    collections::HashMap,
    rc::Rc,
    time::Duration,
};
struct State {
    app: glib::WeakRef<adw::Application>,
    settings: gio::Settings,
    active: Cell<bool>,
    busy: Cell<bool>,
    epoch: Cell<u64>,
    hold: RefCell<Option<gio::ApplicationHoldGuard>>,
    timer: RefCell<Option<glib::SourceId>>,
    tray: RefCell<Option<crate::tray::Tray>>,
    pending: RefCell<Option<gio::Cancellable>>,
    fingerprint: RefCell<Vec<String>>,
}
impl State {
    fn stop(&self) {
        self.active.set(false);
        self.epoch.set(self.epoch.get() + 1);
        if let Some(c) = self.pending.borrow_mut().take() {
            c.cancel();
        }
        if let Some(timer) = self.timer.borrow_mut().take() {
            timer.remove();
        }
        self.tray.borrow_mut().take();
        self.hold.borrow_mut().take();
        if let Some(app) = self.app.upgrade() {
            for window in app.windows() {
                window.set_hide_on_close(false);
            }
            app.withdraw_notification("package-updates");
        }
    }
    fn start(self: &Rc<Self>) {
        self.stop();
        let Some(app) = self.app.upgrade() else {
            return;
        };
        if !self.settings.boolean("background-updates") {
            return;
        }
        *self.hold.borrow_mut() = Some(app.hold());
        let state = self.clone();
        let revision = self.epoch.get();
        let cancellable = gio::Cancellable::new();
        *self.pending.borrow_mut() = Some(cancellable.clone());
        glib::spawn_future_local(async move {
            let result = request_portal(&cancellable, true).await;
            if state.epoch.get() != revision {
                return;
            }
            state.pending.borrow_mut().take();
            match result {
                Ok(connection) => {
                    state.active.set(true);
                    if let Some(app) = state.app.upgrade() {
                        for window in app.windows() {
                            window.set_hide_on_close(true);
                        }
                        if let Ok(tray) = crate::tray::install(&app, &connection) {
                            *state.tray.borrow_mut() = Some(tray);
                        }
                    }
                    state.schedule();
                    state.check();
                }
                Err(error) => {
                    state.stop();
                    let _ = state.settings.set_boolean("background-updates", false);
                    if let Some(app) = state.app.upgrade() {
                        if let Some(window) = app.active_window() {
                            let alert = adw::AlertDialog::new(Some(&gettext("Background Checks Unavailable")), Some(&error));
                            alert.add_response("close", &gettext("Close"));
                            alert.present(Some(&window));
                        } else {
                            eprintln!("Background checks: {error}");
                            app.quit();
                        }
                    }
                }
            }
        });
    }
    fn schedule(self: &Rc<Self>) {
        if let Some(timer) = self.timer.borrow_mut().take() {
            timer.remove();
        }
        let minutes = self.settings.uint("update-check-minutes").clamp(15, 1440);
        let weak = Rc::downgrade(self);
        let timer = glib::timeout_add_local(Duration::from_secs(minutes as u64 * 60), move || {
            if let Some(state) = weak.upgrade() {
                state.check();
                glib::ControlFlow::Continue
            } else {
                glib::ControlFlow::Break
            }
        });
        *self.timer.borrow_mut() = Some(timer);
    }
    fn check(self: &Rc<Self>) {
        if !self.active.get() || self.busy.replace(true) {
            return;
        }
        let state = self.clone();
        let revision = self.epoch.get();
        let aur = self.settings.boolean("aur-enabled");
        let flatpak=self.settings.boolean("flatpak-enabled");let snap=self.settings.boolean("snap-enabled");
        crate::util::background(
            move || {
                let mut updates = athena_core::distro::all_backends()
                    .iter()
                    .filter(|b| (b.key() != "aur" || aur)&&(b.key()!="flatpak"||flatpak)&&(b.key()!="snap"||snap))
                    .flat_map(|b| {
                        b.list_updates()
                            .into_iter()
                            .map(|p| format!("{}:{}:{}", b.key(), p.name, p.available_version))
                            .collect::<Vec<_>>()
                    })
                    .collect::<Vec<_>>();
                updates.sort();
                updates.dedup();
                updates
            },
            move |updates| {
                state.busy.set(false);
                if !state.active.get() {
                    return;
                }
                if state.epoch.get() != revision {
                    state.check();
                    return;
                }
                let Some(app) = state.app.upgrade() else {
                    return;
                };
                if updates.is_empty() {
                    app.withdraw_notification("package-updates");
                    state.fingerprint.borrow_mut().clear();
                    return;
                }
                if *state.fingerprint.borrow() == updates {
                    return;
                }
                let count = updates.len();
                *state.fingerprint.borrow_mut() = updates;
                let notification = gio::Notification::new(&gettext("Software Updates Available"));
                notification.set_body(Some(&crate::i18n::ngettext_format(
                    "{count} package has an update. Open Athena to review and install it.",
                    "{count} packages have updates. Open Athena to review and install them.",
                    count as u32,
                    &[("count", count.to_string())],
                )));
                notification.set_default_action("app.show-updates");
                notification.add_button(&gettext("Review Updates"), "app.show-updates");
                notification.set_icon(&gio::ThemedIcon::new("software-update-available-symbolic"));
                app.send_notification(Some("package-updates"), &notification);
            },
        );
    }
}
async fn request_portal(cancellable: &gio::Cancellable, autostart: bool) -> Result<gio::DBusConnection, String> {
    let connection = gio::bus_get_future(gio::BusType::Session).await.map_err(|e| e.to_string())?;
    let token = format!("athena_{}_{}", std::process::id(), glib::monotonic_time());
    let sender = connection
        .unique_name()
        .ok_or("No session bus identity")?
        .trim_start_matches(':')
        .replace('.', "_");
    let path = format!("/org/freedesktop/portal/desktop/request/{sender}/{token}");
    let (tx, rx) = async_channel::bounded(1);
    let subscription = connection.subscribe_to_signal(
        Some("org.freedesktop.portal.Desktop"),
        Some("org.freedesktop.portal.Request"),
        Some("Response"),
        Some(&path),
        None,
        gio::DBusSignalFlags::NONE,
        move |signal| {
            let _ = tx.try_send(signal.parameters.clone());
        },
    );
    let options = HashMap::from([
        ("handle_token", token.to_variant()),
        (
            "reason",
            gettext("Check for software updates and notify you while Athena is closed").to_variant(),
        ),
        ("autostart", autostart.to_variant()),
        (
            "commandline",
            vec![
                std::env::var("APPIMAGE")
                    .ok()
                    .or_else(|| std::env::current_exe().ok().map(|p| p.to_string_lossy().into_owned()))
                    .unwrap_or_else(|| "athena".into()),
                "--background".into(),
            ]
            .to_variant(),
        ),
        ("dbus-activatable", false.to_variant()),
    ]);
    let params = ("", options).to_variant();
    let (tx, ready) = async_channel::bounded(1);
    connection.call(
        Some("org.freedesktop.portal.Desktop"),
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Background",
        "RequestBackground",
        Some(&params),
        None,
        gio::DBusCallFlags::NONE,
        10000,
        Some(cancellable),
        move |result| {
            let _ = tx.try_send(result);
        },
    );
    let handle = ready
        .recv()
        .await
        .map_err(|e| e.to_string())?
        .map_err(|e| format!("{}: {e}", gettext("The desktop Background portal is unavailable")))?;
    let actual = handle.child_value(0).str().unwrap_or_default().to_owned();
    if actual != path {
        return Err("The Background portal returned an unexpected request handle".into());
    }
    // The portal user interaction may remain pending. Disabling the setting or
    // shutting down closes the request. Only a temporary lifetime hold is used
    // while consent is pending; no checks or tray start before consent.
    let cancel_connection = connection.clone();
    let cancel_path = path.clone();
    let (tx, cancelled) = async_channel::bounded::<()>(1);
    let cancel_id = cancellable.connect_cancelled(move |_| {
        let _ = tx.try_send(());
    });
    let (tx, result) = async_channel::bounded(1);
    glib::spawn_future_local(async move {
        let value = rx.recv().await;
        let _ = tx.send(value).await;
    });
    // Poll without blocking the GTK thread and remain responsive to cancellation.
    let response = loop {
        if cancelled.try_recv().is_ok() {
            cancel_connection.call(
                Some("org.freedesktop.portal.Desktop"),
                &cancel_path,
                "org.freedesktop.portal.Request",
                "Close",
                None,
                None,
                gio::DBusCallFlags::NONE,
                1000,
                None::<&gio::Cancellable>,
                |_| {},
            );
            break Err("Background request cancelled".to_owned());
        }
        if let Ok(value) = result.try_recv() {
            break value.map_err(|e| e.to_string());
        }
        glib::timeout_future(Duration::from_millis(100)).await;
    };
    if let Some(id) = cancel_id {
        cancellable.disconnect_cancelled(id);
    }
    drop(subscription);
    let response = response?;
    let code = response.child_value(0).get::<u32>().unwrap_or(2);
    let results = response.child_value(1).get::<HashMap<String, glib::Variant>>().unwrap_or_default();
    if code != 0 || !results.get("background").and_then(|v| v.get::<bool>()).unwrap_or(false) {
        return Err(gettext("The desktop did not allow Athena to run in the background."));
    }
    Ok(connection)
}
pub fn install(app: &adw::Application) {
    let Some(settings) = crate::settings::load() else {
        return;
    };
    let state = Rc::new(State {
        app: app.downgrade(),
        settings: settings.clone(),
        active: Cell::new(false),
        busy: Cell::new(false),
        epoch: Cell::new(0),
        hold: RefCell::new(None),
        timer: RefCell::new(None),
        tray: RefCell::new(None),
        pending: RefCell::new(None),
        fingerprint: RefCell::new(vec![]),
    });
    let s = state.clone();
    settings.connect_changed(Some("background-updates"), move |settings, _| {
        let revoke = !settings.boolean("background-updates") && (s.active.get() || s.pending.borrow().is_some());
        s.start();
        if revoke {
            glib::spawn_future_local(async {
                let _ = request_portal(&gio::Cancellable::new(), false).await;
            });
        }
    });
    let s = state.clone();
    settings.connect_changed(Some("update-check-minutes"), move |_, _| {
        if s.active.get() {
            s.schedule();
        }
    });
    let s = state.clone();
    app.connect_shutdown(move |_| s.stop());
    let s = state.clone();
    app.connect_window_added(move |_, window| window.set_hide_on_close(s.active.get()));
    state.start();
}

#[cfg(test)]
mod protocol_tests {
    use super::*;
    /// Run on an isolated bus; never replace the real desktop's portal/watcher.
    #[test]
    #[ignore = "requires dbus-run-session and a GTK test display"]
    fn portal_consent_cancellation_and_tray_wire_protocol() {
        assert_eq!(std::env::var("ATHENA_PRIVATE_BUS_TEST").as_deref(), Ok("1"));
        gtk::init().unwrap();
        let context = glib::MainContext::default();
        let _guard = context.acquire().unwrap();
        context.block_on(async {
            let address=std::env::var("DBUS_SESSION_BUS_ADDRESS").unwrap();
            let server=gio::DBusConnection::for_address_future(&address,gio::DBusConnectionFlags::AUTHENTICATION_CLIENT|gio::DBusConnectionFlags::MESSAGE_BUS_CONNECTION,None).await.unwrap();
            for name in ["org.freedesktop.portal.Desktop","org.kde.StatusNotifierWatcher"]{
                let reply=server.call_future(Some("org.freedesktop.DBus"),"/org/freedesktop/DBus","org.freedesktop.DBus","RequestName",Some(&(name,0u32).to_variant()),None,gio::DBusCallFlags::NONE,1000).await.unwrap();assert_eq!(reply.child_value(0).get::<u32>(),Some(1));
            }
            let mode=Rc::new(Cell::new(0u32));let closed=Rc::new(Cell::new(false));let registrations=Rc::new(RefCell::new(vec![]));
            let xml=gio::DBusNodeInfo::for_xml(r#"<node><interface name="org.freedesktop.portal.Background"><method name="RequestBackground"><arg type="s" direction="in"/><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method></interface></node>"#).unwrap();
            let(m,c,r)=(mode.clone(),closed.clone(),registrations.clone());
            let portal=server.register_object("/org/freedesktop/portal/desktop",&xml.interfaces()[0]).method_call(move |connection,sender,_,_,_,params,invocation|{
                let options=params.child_value(1).get::<HashMap<String,glib::Variant>>().unwrap();assert_eq!(options["autostart"].get::<bool>(),Some(true));
                let token=options["handle_token"].get::<String>().unwrap();let sender=sender.unwrap().trim_start_matches(':').replace('.',"_");let path=format!("/org/freedesktop/portal/desktop/request/{sender}/{token}");
                let request_xml=gio::DBusNodeInfo::for_xml(r#"<node><interface name="org.freedesktop.portal.Request"><method name="Close"/><signal name="Response"><arg type="u"/><arg type="a{sv}"/></signal></interface></node>"#).unwrap();let closed=c.clone();
                let id=connection.register_object(&path,&request_xml.interfaces()[0]).method_call(move |_,_,_,_,_,_,invocation|{closed.set(true);invocation.return_value(None);}).build().unwrap();r.borrow_mut().push(id);
                invocation.return_value(Some(&(glib::variant::ObjectPath::try_from(path.as_str()).unwrap(),).to_variant()));
                let response=m.get();if response<2 {glib::idle_add_local_once(move ||{let values=HashMap::from([("background",(response==0).to_variant()),("autostart",(response==0).to_variant())]);connection.emit_signal(None,&path,"org.freedesktop.portal.Request","Response",Some(&(response,values).to_variant())).unwrap();});}
            }).build().unwrap();
            let connection=request_portal(&gio::Cancellable::new(),true).await.unwrap();
            mode.set(1);assert!(request_portal(&gio::Cancellable::new(),true).await.is_err());
            mode.set(2);let cancel=gio::Cancellable::new();let cancel2=cancel.clone();glib::timeout_add_local_once(Duration::from_millis(150),move ||cancel2.cancel());assert!(request_portal(&cancel,true).await.is_err());
            glib::timeout_future(Duration::from_millis(100)).await;assert!(closed.get());
            let watched=Rc::new(Cell::new(false));let watched2=watched.clone();let xml=gio::DBusNodeInfo::for_xml(r#"<node><interface name="org.kde.StatusNotifierWatcher"><method name="RegisterStatusNotifierItem"><arg type="s" direction="in"/></method></interface></node>"#).unwrap();
            let watcher=server.register_object("/StatusNotifierWatcher",&xml.interfaces()[0]).method_call(move |_,_,_,_,_,params,invocation|{assert_eq!(params.child_value(0).get::<String>().as_deref(),Some("/StatusNotifierItem"));watched2.set(true);invocation.return_value(None);}).build().unwrap();
            let app=adw::Application::builder().application_id("io.github.niruxx.Athena.ProtocolTest").build();let updated=Rc::new(Cell::new(false));let updated2=updated.clone();let action=gio::SimpleAction::new("update",None);action.connect_activate(move |_,_|updated2.set(true));app.add_action(&action);app.register(None::<&gio::Cancellable>).unwrap();
            let tray=crate::tray::install(&app,&connection).unwrap();glib::timeout_future(Duration::from_millis(150)).await;assert!(watched.get());
            let dest=connection.unique_name().unwrap();let layout=server.call_future(Some(&dest),"/MenuBar","com.canonical.dbusmenu","GetLayout",Some(&(0i32,-1i32,Vec::<String>::new()).to_variant()),None,gio::DBusCallFlags::NONE,1000).await.unwrap();assert_eq!(layout.type_().as_str(),"(u(ia{sv}av))");assert_eq!(layout.child_value(1).child_value(2).n_children(),3);
            let property=server.call_future(Some(&dest),"/StatusNotifierItem","org.freedesktop.DBus.Properties","Get",Some(&("org.kde.StatusNotifierItem","Menu").to_variant()),None,gio::DBusCallFlags::NONE,1000).await.unwrap();assert_eq!(property.child_value(0).as_variant().unwrap().str(),Some("/MenuBar"));
            server.call_future(Some(&dest),"/MenuBar","com.canonical.dbusmenu","Event",Some(&(2i32,"clicked",0i32.to_variant(),0u32).to_variant()),None,gio::DBusCallFlags::NONE,1000).await.unwrap();assert!(updated.get());drop(tray);
            server.unregister_object(portal).unwrap();server.unregister_object(watcher).unwrap();for id in registrations.borrow_mut().drain(..){server.unregister_object(id).unwrap();}
        });
    }
}
