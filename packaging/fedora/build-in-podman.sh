#!/bin/bash
# Builds the Fedora RPM inside a fedora:latest container (GTK 4 and
# libadwaita from Fedora itself), using Fedora's own Rust. Writes
# dist-fedora/. Run on a host with podman.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$REPO_ROOT/dist-fedora"
mkdir -p "$OUT_DIR"
VERSION="$(sh "$REPO_ROOT/packaging/version.sh" tilde)"

podman run --rm \
    -v "$REPO_ROOT":/repo:ro,Z \
    -v "$OUT_DIR":/output:Z \
    -e VERSION="$VERSION" \
    docker.io/library/fedora:latest \
    bash -c '
        set -euo pipefail
        dnf install -y cargo rust gettext gtk4-devel libadwaita-devel rpm-build
        mkdir /build
        tar -C /repo --exclude=./target --exclude="./dist-*" -cf - . | tar -C /build -xf -
        cd /build
        cargo build --release --locked -p athena
        cargo test --release --locked -p athena-core
        DESTDIR=/stage sh packaging/install.sh target/release/athena
        sed "s/@VERSION@/$VERSION/" packaging/fedora/athena.spec > /tmp/athena.spec
        rpmbuild -bb --define "_stage /stage" --define "_topdir /tmp/rpm" --define "_license_file /build/LICENSE" /tmp/athena.spec
        cp -v /tmp/rpm/RPMS/*/*.rpm /output/
        chmod 666 /output/*.rpm
    '

echo "Fedora package written to $OUT_DIR"
