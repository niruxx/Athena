#!/bin/sh
# Prints Athena's version from Cargo.toml, spelled for a package format:
#   sh packaging/version.sh        4.0.0-alpha.1  (as written)
#   sh packaging/version.sh tilde  4.0.0~alpha.1  (Debian, RPM: sorts before 4.0.0)
#   sh packaging/version.sh arch   4.0.0alpha.1   (pacman: no hyphens allowed)
set -eu
cd "$(dirname "$0")/.."
version=$(sed -n 's/^version = "\(.*\)"/\1/p' Cargo.toml | head -n1)
case "${1:-}" in
    tilde) echo "$version" | tr '-' '~' ;;
    arch) echo "$version" | tr -d '-' ;;
    *) echo "$version" ;;
esac
