#!/bin/bash
# Builds the Debian .deb inside a debian:latest container, since a real
# .deb needs to be linked against Debian's own Qt6/glibc, not the host
# distro's. Run from the repository root on a host with podman. Mirrors
# the "debian" build steps in .github/workflows/c-cpp.yml (which uses
# ubuntu-latest for CI convenience — this uses Debian itself instead).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$REPO_ROOT/dist-debian"
mkdir -p "$OUT_DIR"

podman run --rm \
    -v "$REPO_ROOT":/repo:ro,Z \
    -v "$OUT_DIR":/output:Z \
    docker.io/library/debian:latest \
    bash -c '
        set -euo pipefail
        export DEBIAN_FRONTEND=noninteractive
        apt-get update
        apt-get install -y cmake g++ qt6-base-dev
        cp -r /repo /build
        cd /build
        # The host'"'"'s own build/ (with its own CMakeCache.txt, pointing at
        # the host path) came along with the copy — drop it so this
        # container'"'"'s cmake run starts fresh rather than erroring out over
        # a cache generated somewhere else.
        rm -rf build
        cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
        cmake --build build -j"$(nproc)"
        cd build
        cpack -G DEB
        cp -v ./*.deb /output/
        chmod 666 /output/*.deb
    '

echo "Debian package written to $OUT_DIR"
