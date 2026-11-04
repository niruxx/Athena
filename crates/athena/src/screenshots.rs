//! Debug builds only: `ATHENA_SCREENSHOTS=<dir>` visits each page and saves
//! a PNG of the window, for checking the UI without a display (GTK's
//! Broadway backend) and for the README.

use std::path::PathBuf;
use std::time::Duration;

use adw::prelude::*;
use gtk::glib;

fn save(window: &adw::ApplicationWindow, path: &PathBuf) {
    let paintable = gtk::WidgetPaintable::new(Some(window));
    let (width, height) = (window.width() as f64, window.height() as f64);
    let snapshot = gtk::Snapshot::new();
    paintable.snapshot(&snapshot, width, height);
    let Some(node) = snapshot.to_node() else { return };
    let Some(renderer) = window.native().and_then(|n| n.renderer()) else { return };
    let texture = renderer.render_texture(&node, None);
    if let Err(error) = texture.save_to_png(path) {
        eprintln!("screenshot {}: {error}", path.display());
    }
}

/// One step: an action (select a page, type a search), then a wait for
/// the page to load, then a PNG named after it.
pub type Step = (&'static str, Box<dyn Fn()>);

pub fn run(window: adw::ApplicationWindow, dir: PathBuf, steps: Vec<Step>) {
    glib::spawn_future_local(async move {
        let _ = std::fs::create_dir_all(&dir);
        for (name, act) in steps {
            act();
            glib::timeout_future(Duration::from_secs(6)).await;
            save(&window, &dir.join(format!("{name}.png")));
        }
        if let Some(app) = window.application() {
            app.quit();
        }
    });
}
