#!/bin/sh
# Installs a built Athena under $DESTDIR$PREFIX (default /usr): the binary,
# its desktop entry, GSettings schema, and icons. Every package (Arch, Debian, Fedora) uses
# this, so they lay files out the same way. Run from the repository root:
#   DESTDIR=/tmp/stage sh packaging/install.sh target/release/athena
set -eu
BIN="${1:-target/release/athena}"
PREFIX="${PREFIX:-/usr}"
ROOT="${DESTDIR:-}$PREFIX"
APP_ID=io.github.niruxx.Athena

install -Dm755 "$BIN" "$ROOT/bin/athena"
install -Dm644 "data/$APP_ID.desktop" "$ROOT/share/applications/$APP_ID.desktop"
install -Dm644 "data/$APP_ID.gschema.xml" "$ROOT/share/glib-2.0/schemas/$APP_ID.gschema.xml"
install -Dm644 "data/icons/$APP_ID.svg" "$ROOT/share/icons/hicolor/scalable/apps/$APP_ID.svg"
for size in 64 128 256; do
    install -Dm644 "data/icons/icon-$size.png" "$ROOT/share/icons/hicolor/${size}x$size/apps/$APP_ID.png"
done
