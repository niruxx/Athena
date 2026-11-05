#!/bin/sh
# Run from any directory; requires gettext >= 0.26 (Rust extraction support).
set -eu
cd "$(dirname "$0")/.."
find crates/athena/src -name '*.rs' -print | sort > po/POTFILES.in
xgettext --language=Rust --from-code=UTF-8 --keyword=gettext --keyword=gettext_noop --keyword=crate::i18n::gettext_noop --keyword=crate::i18n::gettext --keyword=gettext_format:1 --keyword=crate::i18n::ngettext:1,2 --keyword=ngettext_format:1,2 --package-name=Athena --package-version="$(sh packaging/version.sh)" --msgid-bugs-address=https://github.com/niruxx/Athena/issues --copyright-holder='Athena contributors' --files-from=po/POTFILES.in --output=po/athena.pot
for language in $(cat po/LINGUAS); do
    if test -f "po/$language.po"; then msgmerge --update --backup=none "po/$language.po" po/athena.pot; fi
done
