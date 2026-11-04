//! What every page shares: the package sources, the window, toasts, and
//! who to tell when packages change.

use std::cell::RefCell;
use std::rc::Rc;
use std::sync::Arc;

use athena_core::Backend;

/// One package from one source, as a list item (wrapped in a
/// `glib::BoxedAnyObject` for list models).
#[derive(Debug, Clone)]
pub struct Item {
    pub source: usize,
    pub package: athena_core::PackageInfo,
}

pub struct Context {
    pub backends: Vec<Arc<dyn Backend>>,
    pub navigation: adw::NavigationView,
    pub window: adw::ApplicationWindow,
    pub toasts: adw::ToastOverlay,
    listeners: RefCell<Vec<Box<dyn Fn()>>>,
}

impl Context {
    pub fn new(backends: Vec<Arc<dyn Backend>>, window: adw::ApplicationWindow, toasts: adw::ToastOverlay) -> Rc<Self> {
        Rc::new(Self { backends, window, toasts, navigation: adw::NavigationView::new(), listeners: RefCell::default() })
    }

    pub fn toast(&self, text: &str) {
        self.toasts.add_toast(adw::Toast::new(text));
    }

    /// Called after anything installs, removes, or updates packages.
    pub fn on_packages_changed(&self, listener: impl Fn() + 'static) {
        self.listeners.borrow_mut().push(Box::new(listener));
    }

    pub fn packages_changed(&self) {
        for listener in self.listeners.borrow().iter() {
            listener();
        }
    }

    pub fn source_name(&self, source: usize) -> &'static str {
        match self.backends[source].key() {
            "pacman" => "pacman",
            "dnf" => "DNF",
            "apt" => "APT",
            "flatpak" => "Flatpak",
            "snap" => "Snap",
            other => other,
        }
    }
}
