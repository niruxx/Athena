//! Catalog labels and bitmap icons shared by the grid and package rows.
use gtk::prelude::*;

pub fn set_icon(image: &gtk::Image, icon: &str, fallback: &str) {
    image.set_widget_name(icon);
    image.set_icon_name(Some(&crate::util::package_icon(fallback)));
    if icon.starts_with("https://") {
        let expected = icon.to_owned();
        let (url, cache, weak) = (icon.to_owned(), gtk::glib::user_cache_dir().join("athena/icons"), image.downgrade());
        // A recycled row must not receive the previous application's icon.
        image.set_widget_name(icon);
        crate::util::background(
            move || athena_core::media::cache_image(&url, &cache),
            move |path| {
                if let (Some(image), Some(path)) = (weak.upgrade(), path)
                    && image.widget_name() == expected
                {
                    set_file(&image, &path);
                }
            },
        );
    } else if std::path::Path::new(icon).is_absolute() {
        image.set_widget_name(icon);
        set_file(image, std::path::Path::new(icon));
    } else if !icon.is_empty() {
        image.set_widget_name(icon);
        image.set_icon_name(Some(icon));
    }
}
fn set_file(image: &gtk::Image, path: &std::path::Path) {
    // GdkPixbuf's installed loaders include JPEG XL where the distribution ships one.
    if let Ok(pixbuf) = gtk::gdk_pixbuf::Pixbuf::from_file_at_scale(path, 128, 128, true) {
        image.set_paintable(Some(&gtk::gdk::Texture::for_pixbuf(&pixbuf)));
    } else {
        let (path, cache, weak, expected) = (
            path.to_owned(),
            gtk::glib::user_cache_dir().join("athena/decoded-icons"),
            image.downgrade(),
            image.widget_name(),
        );
        crate::util::background(
            move || athena_core::media::decoded_image(&path, &cache),
            move |path| {
                if let (Some(image), Some(path)) = (weak.upgrade(), path)
                    && image.widget_name() == expected
                    && let Ok(pixbuf) = gtk::gdk_pixbuf::Pixbuf::from_file_at_scale(path, 128, 128, true)
                {
                    image.set_paintable(Some(&gtk::gdk::Texture::for_pixbuf(&pixbuf)));
                }
            },
        );
    }
}

pub fn app_icon(id: &str, pixels:i32)->gtk::Image {
    let image=gtk::Image::builder().pixel_size(pixels).build();
    let catalog=athena_core::catalog::current();
    set_icon(&image,catalog.find(id).map(|app|app.icon.as_str()).unwrap_or_default(),id);
    if let Some(app)=crate::desktop::find(id)&&let Some(icon)=app.icon(){image.set_from_gicon(&icon);}
    image
}
pub fn app_name(id:&str)->String {
    crate::desktop::find(id).map(|app|app.display_name().to_string()).or_else(||athena_core::catalog::current().find(id).map(|app|app.name.clone())).unwrap_or_else(||id.into())
}
