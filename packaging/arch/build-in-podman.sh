#!/bin/bash
# Builds the Arch Linux package inside an archlinux:latest container, since
# a real Arch .pkg.tar.zst needs to be linked against Arch's own Qt6/glibc,
# not the host distro's. Run from the repository root on a host with podman.
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
        pacman -Syu --noconfirm --needed base-devel cmake qt6-base polkit
        useradd -m builder
        cp -r /repo /home/builder/repo
        chown -R builder:builder /home/builder/repo
        chmod 777 /output
        su builder -c "cd /home/builder/repo/packaging/arch && BUILDDIR=/home/builder/build PKGDEST=/output makepkg -f --noconfirm"
    '

echo "Arch package written to $OUT_DIR"
