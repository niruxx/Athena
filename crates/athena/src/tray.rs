//! StatusNotifierItem plus the standard DBusMenu used by KDE and other hosts.
use adw::prelude::*;
use gtk::{gio, glib};
use std::collections::HashMap;
const ITEM: &str = r#"<node><interface name="org.kde.StatusNotifierItem">
<property name="Category" type="s" access="read"/><property name="Id" type="s" access="read"/><property name="Title" type="s" access="read"/>
<property name="IconThemePath" type="s" access="read"/><property name="AttentionMovieName" type="s" access="read"/><property name="Status" type="s" access="read"/><property name="WindowId" type="i" access="read"/>
<property name="IconName" type="s" access="read"/><property name="IconPixmap" type="a(iiay)" access="read"/>
<property name="AttentionIconName" type="s" access="read"/><property name="AttentionIconPixmap" type="a(iiay)" access="read"/>
<property name="OverlayIconName" type="s" access="read"/><property name="OverlayIconPixmap" type="a(iiay)" access="read"/>
<property name="ToolTip" type="(sa(iiay)ss)" access="read"/><property name="ItemIsMenu" type="b" access="read"/><property name="Menu" type="o" access="read"/>
<method name="Activate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
<method name="SecondaryActivate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
<method name="ContextMenu"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
<method name="Scroll"><arg type="i" direction="in"/><arg type="s" direction="in"/></method>
<signal name="NewIcon"/><signal name="NewStatus"><arg type="s"/></signal></interface></node>"#;
const MENU: &str = r#"<node><interface name="com.canonical.dbusmenu">
<property name="Version" type="u" access="read"/><property name="TextDirection" type="s" access="read"/><property name="Status" type="s" access="read"/><property name="IconThemePath" type="as" access="read"/>
<method name="GetLayout"><arg type="i" direction="in"/><arg type="i" direction="in"/><arg type="as" direction="in"/><arg type="u" direction="out"/><arg type="(ia{sv}av)" direction="out"/></method>
<method name="GetGroupProperties"><arg type="ai" direction="in"/><arg type="as" direction="in"/><arg type="a(ia{sv})" direction="out"/></method>
<method name="GetProperty"><arg type="i" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="out"/></method>
<method name="Event"><arg type="i" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="in"/><arg type="u" direction="in"/></method>
<method name="EventGroup"><arg type="a(isvu)" direction="in"/><arg type="ai" direction="out"/></method>
<method name="AboutToShow"><arg type="i" direction="in"/><arg type="b" direction="out"/></method>
<method name="AboutToShowGroup"><arg type="ai" direction="in"/><arg type="ai" direction="out"/><arg type="ai" direction="out"/></method>
<signal name="LayoutUpdated"><arg type="u"/><arg type="i"/></signal></interface></node>"#;
pub struct Tray {
    connection: gio::DBusConnection,
    objects: Vec<gio::RegistrationId>,
    watchers: Vec<Box<dyn FnOnce()>>,
}
impl Drop for Tray {
    fn drop(&mut self) {
        for id in self.objects.drain(..) {
            let _ = self.connection.unregister_object(id);
        }
        for unwatch in self.watchers.drain(..) {
            unwatch();
        }
    }
}
fn properties(id: i32) -> HashMap<String, glib::Variant> {
    let mut props = HashMap::new();
    if id == 0 {
        props.insert("children-display".into(), "submenu".to_variant());
    } else {
        let text = match id {
            1 => crate::i18n::gettext("Open Athena"),
            2 => crate::i18n::gettext("Update…"),
            3 => crate::i18n::gettext("Quit"),
            _ => String::new(),
        };
        props.insert("label".into(), text.to_variant());
        props.insert("enabled".into(), true.to_variant());
        props.insert("visible".into(), true.to_variant());
    }
    props
}
fn layout(id: i32, depth: i32) -> glib::Variant {
    let children = if id == 0 && depth != 0 {
        (1..=3).map(|id| layout(id, 0)).collect::<Vec<_>>()
    } else {
        vec![]
    };
    (id, properties(id), children).to_variant()
}
fn event(app: &adw::Application, id: i32, event: &str) {
    if event == "clicked" {
        match id {
            1 => app.activate(),
            2 => app.activate_action("update", None),
            3 => app.activate_action("quit", None),
            _ => {}
        }
    }
}
pub fn install(app: &adw::Application, connection: &gio::DBusConnection) -> Result<Tray, glib::Error> {
    let info = gio::DBusNodeInfo::for_xml(ITEM)?;
    let weak = app.downgrade();
    let item = connection
        .register_object("/StatusNotifierItem", &info.interfaces()[0])
        .method_call(move |_, _, _, _, method, _, invocation| {
            if let Some(app) = weak.upgrade() {
                match method {
                    "Activate" | "SecondaryActivate" => app.activate(),
                    "ContextMenu" => {}
                    "Scroll" => {}
                    _ => {
                        invocation.return_dbus_error("org.freedesktop.DBus.Error.UnknownMethod", "Unknown tray method");
                        return;
                    }
                }
            }
            invocation.return_value(None);
        })
        .property(|_, _, _, _, name| match name {
            "Category" => "SystemServices".to_variant(),
            "Id" => crate::APP_ID.to_variant(),
            "Title" => "Athena".to_variant(),
            "Status" => "Active".to_variant(),
            "WindowId" => 0i32.to_variant(),
            "IconName" => "system-software-update".to_variant(),
            "AttentionIconName" | "OverlayIconName" | "AttentionMovieName" | "IconThemePath" => "".to_variant(),
            "IconPixmap" | "AttentionIconPixmap" | "OverlayIconPixmap" => Vec::<(i32, i32, Vec<u8>)>::new().to_variant(),
            "ToolTip" => (
                "system-software-update",
                Vec::<(i32, i32, Vec<u8>)>::new(),
                "Athena",
                crate::i18n::gettext("Check and install software updates"),
            )
                .to_variant(),
            "ItemIsMenu" => false.to_variant(),
            "Menu" => glib::variant::ObjectPath::try_from("/MenuBar").unwrap().to_variant(),
            _ => "".to_variant(),
        })
        .build()?;
    let info = gio::DBusNodeInfo::for_xml(MENU)?;
    let weak = app.downgrade();
    let menu = match connection
        .register_object("/MenuBar", &info.interfaces()[0])
        .method_call(move |_, _, _, _, method, params, invocation| {
            let result = match method {
                "GetLayout" => {
                    let id = params.child_value(0).get::<i32>().unwrap_or(0);
                    let depth = params.child_value(1).get::<i32>().unwrap_or(-1);
                    glib::Variant::tuple_from_iter([1u32.to_variant(), layout(id, depth)])
                }
                "GetGroupProperties" => {
                    let ids = params.child_value(0).get::<Vec<i32>>().unwrap_or_default();
                    (ids.into_iter().map(|id| (id, properties(id))).collect::<Vec<_>>(),).to_variant()
                }
                "GetProperty" => {
                    let id = params.child_value(0).get::<i32>().unwrap_or(0);
                    let key = params.child_value(1).get::<String>().unwrap_or_default();
                    (properties(id).remove(&key).unwrap_or_else(|| "".to_variant()),).to_variant()
                }
                "Event" => {
                    if let Some(app) = weak.upgrade() {
                        event(
                            &app,
                            params.child_value(0).get::<i32>().unwrap_or(0),
                            &params.child_value(1).get::<String>().unwrap_or_default(),
                        );
                    }
                    ().to_variant()
                }
                "EventGroup" => {
                    if let Some(app) = weak.upgrade() {
                        for e in params.child_value(0).iter() {
                            event(
                                &app,
                                e.child_value(0).get::<i32>().unwrap_or(0),
                                &e.child_value(1).get::<String>().unwrap_or_default(),
                            );
                        }
                    }
                    (Vec::<i32>::new(),).to_variant()
                }
                "AboutToShow" => (false,).to_variant(),
                "AboutToShowGroup" => (Vec::<i32>::new(), Vec::<i32>::new()).to_variant(),
                _ => {
                    invocation.return_dbus_error("org.freedesktop.DBus.Error.UnknownMethod", "Unknown menu method");
                    return;
                }
            };
            invocation.return_value(Some(&result));
        })
        .property(|_, _, _, _, name| match name {
            "Version" => 3u32.to_variant(),
            "TextDirection" => "ltr".to_variant(),
            "Status" => "normal".to_variant(),
            "IconThemePath" => Vec::<String>::new().to_variant(),
            _ => "".to_variant(),
        })
        .build()
    {
        Ok(menu) => menu,
        Err(error) => {
            let _ = connection.unregister_object(item);
            return Err(error);
        }
    };
    let watchers = ["org.kde.StatusNotifierWatcher", "org.freedesktop.StatusNotifierWatcher"]
        .into_iter()
        .map(|watcher| {
            let id = gio::bus_watch_name_on_connection(
                connection,
                watcher,
                gio::BusNameWatcherFlags::NONE,
                move |connection, _, _| {
                    connection.call(
                        Some(watcher),
                        "/StatusNotifierWatcher",
                        watcher,
                        "RegisterStatusNotifierItem",
                        Some(&("/StatusNotifierItem",).to_variant()),
                        None,
                        gio::DBusCallFlags::NONE,
                        5000,
                        None::<&gio::Cancellable>,
                        |_| {},
                    );
                },
                |_, _| {},
            );
            Box::new(move || gio::bus_unwatch_name(id)) as Box<dyn FnOnce()>
        })
        .collect();
    Ok(Tray {
        connection: connection.clone(),
        objects: vec![item, menu],
        watchers,
    })
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn menu_matches_wire_signature() {
        let tree = layout(0, -1);
        assert_eq!(tree.type_().as_str(), "(ia{sv}av)");
        assert_eq!(tree.child_value(2).n_children(), 3);
        assert_eq!(layout(2, 0).child_value(2).n_children(), 0);
        assert!(gio::DBusNodeInfo::for_xml(ITEM).is_ok());
        assert!(gio::DBusNodeInfo::for_xml(MENU).is_ok());
    }
}
