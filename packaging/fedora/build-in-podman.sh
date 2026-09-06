#!/bin/bash
# Builds the Fedora .rpm inside a fedora:latest container, since a real RPM
# needs to be linked against Fedora's own Qt6/glibc, not the host distro's.
# Run from the repository root on a host with podman. Mirrors the "fedora"
# job in .github/workflows/c-cpp.yml.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$REPO_ROOT/dist-fedora"
mkdir -p "$OUT_DIR"

podman run --rm \
    -v "$REPO_ROOT":/repo:ro,Z \
    -v "$OUT_DIR":/output:Z \
    docker.io/library/fedora:latest \
    bash -c '
        set -euo pipefail
        dnf install -y cmake gcc-c++ qt6-qtbase-devel rpm-build
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
        cpack -G RPM
        cp -v ./*.rpm /output/
        chmod 666 /output/*.rpm
    '

echo "Fedora package written to $OUT_DIR"
