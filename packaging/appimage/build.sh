#!/bin/bash
# Build on Ubuntu 24.04 (the oldest supported GTK/glibc baseline).
set -euo pipefail
cd "$(dirname "$0")/../.."
athena_root="$PWD"
. /etc/os-release
if [[ "$ID" != ubuntu || "$VERSION_ID" != 24.04 ]]; then
    echo "Build AppImages on Ubuntu 24.04 using the appimage CI job. Newer GTK/Glycin loaders and glibc need separate bundling support." >&2
    exit 1
fi
athena_out="${ATHENA_APPIMAGE_OUTPUT:-$athena_root/dist-appimage}"
athena_work="${ATHENA_APPIMAGE_WORK:-$(mktemp -d)}"
mkdir -p "$athena_out" "$athena_work"
python3 packaging/appimage/fetch-tools.py "$athena_work/tools"
if [[ "${ATHENA_SKIP_BUILD:-0}" != 1 ]]; then cargo build --release --locked -p athena; fi
DESTDIR="$athena_work/AppDir" sh packaging/install.sh "${ATHENA_BINARY:-target/release/athena}"
glib-compile-schemas --strict "$athena_work/AppDir/usr/share/glib-2.0/schemas"
export DEPLOY_GTK_VERSION=4
export APPIMAGE_EXTRACT_AND_RUN=1
export LINUXDEPLOY_OUTPUT_VERSION="$(sh packaging/version.sh)"
export OUTPUT="$athena_out/Athena-$LINUXDEPLOY_OUTPUT_VERSION-x86_64.AppImage"
export PATH="$athena_work/tools:$PATH"
"$athena_work/tools/linuxdeploy-x86_64.AppImage" --appdir "$athena_work/AppDir" \
    --desktop-file data/io.github.niruxx.Athena.desktop \
    --icon-file data/icons/io.github.niruxx.Athena.svg \
    --custom-apprun packaging/appimage/AppRun --plugin gtk --output appimage
sha256sum "$OUTPUT" > "$OUTPUT.sha256"
echo "AppImage: $OUTPUT"
