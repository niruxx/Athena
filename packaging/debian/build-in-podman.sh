#!/bin/bash
# Builds the Debian package inside a debian:stable container (GTK 4 and
# libadwaita from Debian itself), using Debian's own Rust. Writes
# dist-debian/. Run on a host with podman.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$REPO_ROOT/dist-debian"
mkdir -p "$OUT_DIR"
VERSION="$(sh "$REPO_ROOT/packaging/version.sh" tilde)"

podman run --rm \
    -v "$REPO_ROOT":/repo:ro,Z \
    -v "$OUT_DIR":/output:Z \
    -e VERSION="$VERSION" \
    docker.io/library/debian:stable \
    bash -c '
        set -euo pipefail
        export DEBIAN_FRONTEND=noninteractive
        apt-get update
        apt-get install -y --no-install-recommends curl build-essential gettext pkg-config libgtk-4-dev libadwaita-1-dev ca-certificates
        curl --proto "=https" --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain 1.92.0
        . "$HOME/.cargo/env"
        mkdir /build
        tar -C /repo --exclude=./target --exclude="./dist-*" -cf - . | tar -C /build -xf -
        cd /build
        cargo build --release --locked -p athena
        cargo test --release --locked -p athena-core
        DESTDIR=/stage sh packaging/install.sh target/release/athena
        install -Dm644 LICENSE /stage/usr/share/doc/athena/copyright
        ARCH="$(dpkg --print-architecture)"
        mkdir -p /stage/DEBIAN
        cat > /stage/DEBIAN/control <<CONTROL
Package: athena
Version: $VERSION
Section: admin
Priority: optional
Architecture: $ARCH
Maintainer: niruxxdaboi <perezkyle98@protonmail.com>
Depends: curl, libgtk-4-1 (>= 4.14), libadwaita-1-0 (>= 1.5), libc6, pkexec | policykit-1, policykit-1-gnome | polkit-1-auth-agent
Suggests: flatpak, snapd, appstream, libjxl-tools, snapper, timeshift
Homepage: https://github.com/niruxx/Athena
Description: package manager for APT, Flatpak, and Snap
 Install, update, and remove software from every source on the system
 in one GTK app: searches all of them at once, lists everything installed,
 and runs each change in a window with its progress and full output.
CONTROL
        dpkg-deb --build --root-owner-group /stage "/output/athena_${VERSION}_${ARCH}.deb"
        chmod 666 /output/*.deb
    '

echo "Debian package written to $OUT_DIR"
