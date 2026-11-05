#!/bin/bash
# Builds the Arch Linux package inside an archlinux:latest container, so it
# links against Arch's own GTK and glibc rather than the host's. Run from
# anywhere on a host with podman; writes dist-arch/.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$REPO_ROOT/dist-arch"
mkdir -p "$OUT_DIR"

podman run --rm \
    -v "$REPO_ROOT":/repo:ro,Z \
    -v "$OUT_DIR":/output:Z \
    docker.io/library/archlinux:latest \
    bash -c '
        set -euo pipefail
        pacman -Syu --noconfirm --needed base-devel rust gettext gtk4 libadwaita polkit polkit-gnome
        useradd -m builder
        mkdir /home/builder/repo
        # The source without build output (target/ can be gigabytes).
        tar -C /repo --exclude=./target --exclude="./dist-*" -cf - . | tar -C /home/builder/repo -xf -
        chown -R builder:builder /home/builder/repo
        chmod 777 /output
        su builder -c "cd /home/builder/repo/packaging/arch && BUILDDIR=/home/builder/build PKGDEST=/output makepkg -f --noconfirm"
    '

echo "Arch package written to $OUT_DIR"
