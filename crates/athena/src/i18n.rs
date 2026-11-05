//! GNU gettext catalogs. Initialize before GTK starts any worker threads.
use std::path::PathBuf;
const DOMAIN: &str = "athena";
pub fn init() {
    gettextrs::setlocale(gettextrs::LocaleCategory::LcAll, "");
    let directory = std::env::var_os("ATHENA_LOCALEDIR")
        .map(PathBuf::from)
        .or_else(|| {
            std::env::current_exe()
                .ok()
                .and_then(|p| p.parent()?.parent().map(|p| p.join("share/locale")))
                .filter(|p| p.is_dir())
        })
        .unwrap_or_else(|| PathBuf::from("/usr/share/locale"));
    let _ = gettextrs::bindtextdomain(DOMAIN, directory.to_string_lossy().as_ref());
    let _ = gettextrs::bind_textdomain_codeset(DOMAIN, "UTF-8");
    let _ = gettextrs::textdomain(DOMAIN);
}
pub const fn gettext_noop(message: &str) -> &str {
    message
}
pub fn gettext(message: &str) -> String {
    gettextrs::dgettext(DOMAIN, message)
}
/// Named placeholders can be reordered in translations without Rust format syntax.
pub fn gettext_format(message: &str, values: &[(&str, String)]) -> String {
    let translated = gettext(message);
    let valid = values.iter().all(|(key, _)| translated.contains(&format!("{{{key}}}")));
    let mut result = if valid { translated } else { message.to_owned() };
    for (key, value) in values {
        result = result.replace(&format!("{{{key}}}"), value);
    }
    result
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn substitutes_named_fields() {
        assert_eq!(
            gettext_format("{count} updates from {source}", &[("count", "3".into()), ("source", "APT".into())]),
            "3 updates from APT"
        );
    }
}

pub fn ngettext_format(singular: &str, plural: &str, count: u32, values: &[(&str, String)]) -> String {
    let translated = gettextrs::dngettext(DOMAIN, singular, plural, count);
    let fallback = if count == 1 { singular } else { plural };
    let mut text = if values.iter().all(|(key, _)| translated.contains(&format!("{{{key}}}"))) {
        translated
    } else {
        fallback.to_owned()
    };
    for (key, value) in values {
        text = text.replace(&format!("{{{key}}}"), value);
    }
    text
}
