//! Running backend work off the main thread, and small widget helpers.

use gtk::glib;
use adw::prelude::*;

/// Runs `work` on a worker thread, then `done` with its result back on the
/// main thread. Backend calls block on subprocesses, so they all go
/// through here.
pub fn background<T, W, D>(work: W, done: D)
where
    T: Send + 'static,
    W: FnOnce() -> T + Send + 'static,
    D: FnOnce(T) + 'static,
{
    glib::spawn_future_local(async move {
        if let Ok(result) = gtk::gio::spawn_blocking(work).await {
            done(result);
        }
    });
}

/// A label with a style class, left-aligned, ellipsized.
pub fn label(text: &str, classes: &[&str]) -> gtk::Label {
    let label = gtk::Label::builder().label(text).xalign(0.0).ellipsize(gtk::pango::EllipsizeMode::End).build();
    for class in classes {
        label.add_css_class(class);
    }
    label
}

/// A spinner page for "loading…".
pub fn loading_page(title: &str) -> adw::StatusPage {
    let spinner = gtk::Spinner::builder()
        .spinning(true)
        .width_request(32)
        .height_request(32)
        .halign(gtk::Align::Center)
        .build();
    let page = adw::StatusPage::builder().title(title).child(&spinner).build();
    page.add_css_class("page-enter");
    page
}

/// The icon for a package: the app's own icon when the icon theme has one
/// under its name (Flatpak app IDs, most desktop apps), else a generic one.
pub fn package_icon(name: &str) -> String {
    let display = gtk::gdk::Display::default();
    let has = display.is_some_and(|d| gtk::IconTheme::for_display(&d).has_icon(name));
    if has { name.to_owned() } else { "package-x-generic-symbolic".to_owned() }
}

/// Place actions in their own padded row instead of stacking touching buttons.
pub fn action_row(title: &str, description: &str, label: &str, clicked: impl Fn()+'static) -> adw::ActionRow {
    let row=adw::ActionRow::builder().use_markup(false).build();row.set_title(title);row.set_subtitle(description);
    let button=gtk::Button::builder().label(label).valign(gtk::Align::Center).build();button.add_css_class("row-action");button.connect_clicked(move |_|clicked());row.add_suffix(&button);row
}

pub fn themed_icon(candidates:&[&str])->String {
    if let Some(display)=gtk::gdk::Display::default(){let theme=gtk::IconTheme::for_display(&display);if let Some(name)=candidates.iter().find(|name|theme.has_icon(name)){return (*name).into();}}
    "application-x-executable-symbolic".into()
}

/// Athena's own logo, bundled into the binary so it always renders even when
/// the icon theme has no entry for the application ID (development builds,
/// AppImages, sandboxed IDEs).
pub fn app_logo() -> gtk::gdk::Texture {
    thread_local! {
        static LOGO: gtk::gdk::Texture = gtk::gdk::Texture::from_bytes(&glib::Bytes::from_static(include_bytes!("../../../data/icons/icon-256.png")))
            .expect("bundled logo is a valid PNG");
    }
    LOGO.with(|logo| logo.clone())
}

/// A large logo image for status pages and welcome screens.
pub fn logo_image(pixels: i32) -> gtk::Image {
    let image = gtk::Image::builder().paintable(&app_logo()).pixel_size(pixels).build();
    image.add_css_class("setup-logo");
    image
}

/// A single source picker ("All sources", pacman, Flatpak, …) as a drop-down,
/// so it sits on one line beside the other filters.
pub fn source_dropdown(ctx: &crate::context::Context, changed: impl Fn(Option<usize>) + 'static) -> gtk::DropDown {
    let mut labels = vec![crate::i18n::gettext("All sources")];
    labels.extend((0..ctx.backends.len()).map(|source| ctx.source_name(source).to_owned()));
    let dropdown = gtk::DropDown::from_strings(&labels.iter().map(String::as_str).collect::<Vec<_>>());
    dropdown.set_tooltip_text(Some(&crate::i18n::gettext("Package source")));
    dropdown.set_valign(gtk::Align::Center);
    dropdown.connect_selected_notify(move |dropdown| {
        let selected = dropdown.selected();
        changed(if selected == 0 || selected == gtk::INVALID_LIST_POSITION { None } else { Some(selected as usize - 1) });
    });
    dropdown
}

/// Replays an entrance animation (a CSS class with an `animation`) on a widget.
pub fn animate(widget: &impl IsA<gtk::Widget>, class: &str) {
    let widget = widget.as_ref().clone();
    widget.remove_css_class(class);
    let class = class.to_owned();
    glib::idle_add_local_once(move || widget.add_css_class(&class));
}

/// A stack that crossfades or slides between its children instead of cutting.
pub fn animated_stack() -> gtk::Stack {
    gtk::Stack::builder()
        .transition_type(gtk::StackTransitionType::Crossfade)
        .transition_duration(220)
        .build()
}

/// Adds a tinted leading icon to a row, so neighbouring rows are easy to tell apart.
pub fn with_icon<R: IsA<adw::ActionRow>>(row: R, icon: &str) -> R {
    let image = gtk::Image::builder().icon_name(icon).valign(gtk::Align::Center).build();
    image.add_css_class("permission-icon");
    row.as_ref().add_prefix(&image);
    row
}
