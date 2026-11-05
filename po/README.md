# Translating Athena

The UI uses GNU gettext's `athena` domain and UTF-8 catalogs. English is the
fallback. `es.po` is a starter Spanish catalog; untranslated messages fall back
to English. Package descriptions and manager output keep the source's language.

With GNU gettext 0.26 or newer (Rust support), run:

```
sh scripts/update-translations.sh
msginit --input=po/athena.pot --locale=LANGUAGE --output-file=po/LANGUAGE.po
msgfmt --check --check-format po/LANGUAGE.po -o /tmp/athena.mo
```

Add the language code to `po/LINGUAS`. Keep `{named}` placeholders unchanged;
their positions can change. Do not translate package identifiers or settings keys.
`packaging/install.sh` compiles and installs each catalog. To test without installing:

```
mkdir -p /tmp/athena-locale/es/LC_MESSAGES
msgfmt po/es.po -o /tmp/athena-locale/es/LC_MESSAGES/athena.mo
LANGUAGE=es ATHENA_LOCALEDIR=/tmp/athena-locale cargo run -p athena
```

`ATHENA_LOCALEDIR` also supports relocatable bundles. Applications find a sibling
`share/locale` automatically. Further UI strings and translations can be added as
reviewed contributions; backend diagnostic text is not translated by gettext.
