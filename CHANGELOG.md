# Changelog

All notable changes to Athena are documented in this file. Newest release first;
append new entries above as changes land.

## [2.0.0] - 2026-08-27

### Added

- **System tray integration**: a tray icon appears when package updates are
  available (or always, per a new setting), with a right-click menu for
  Settings, Update (installs everything pending across every backend in one
  confirm-then-run transaction), and Quit. Polls in the background on a
  configurable interval.
- **Preferences reorganized into tabs**: General, System, Layout, and Logging
  Options, replacing the single flat settings list.
  - **System**: run transactions automatically without a confirmation prompt
    (off by default), hide the tray icon when there are no updates, the
    update-check interval (minutes), and metadata expire time (hours, honored
    by the DNF backend via `--setopt=metadata_expire`).
  - **Layout**: Disable Group view, to hide the category-browser tab.
  - **Logging Options**: enable logging to a file, choose the destination
    folder, and set a minimum severity level (Error/Warning/Info/Debug).
- **Advanced search**: the Search page gained a "Dependency Query" mode that
  searches by capability instead of name/keyword — packages that *provide* or
  *require* a given capability — plus **Repository** and **Architecture**
  filters that narrow the currently-shown results. Dependency Query is fully
  implemented for DNF and APT, partially for Pacman (Requires only, since
  Pacman has no safe non-interactive Provides query), and gracefully returns
  no results on Flatpak/Snap, which have no comparable capability graph.
- **Download without installing**: "Download Selected" and "Download Selected
  + Dependencies" on the Search page fetch a copy of the checked packages
  (optionally including their not-yet-installed dependencies) into a chosen
  folder, without installing them. Unprivileged on DNF, APT, and Snap;
  Pacman and Flatpak still go through the existing privilege model.
- **Architecture and Size columns** in every package list table, off by
  default and toggled from the table header's own right-click menu.
- **Homepage link** shown in a package's details panel when the backend
  reports one (DNF, APT, and Pacman today).
- Optional **file logging**, installed as a Qt message handler so it also
  captures Qt's own internal warnings, not just app-authored log lines.

### Changed

- `PackageBackend`'s interface gained `dependencyQuery()` and
  `downloadCommands()`/`downloadPackages()`, implemented across all five
  backends (DNF, APT, Pacman, Flatpak, Snap).
- `ProcessRunner::Command` now carries an optional working directory, since
  `apt-get download` and `snap download` only know how to write output
  relative to their current directory rather than accepting a destination
  flag.
- Version bumped to 2.0.0.

### Fixed

- The Search page's download buttons now correctly reflect already-checked
  (installed) rows immediately after a search or filter change, instead of
  only updating after a manual checkbox click.

### Packaging

- Fedora (`.rpm`), Debian/Ubuntu (`.deb`), and a portable Arch Linux tarball
  (`.tar.gz`) all build cleanly via CPack.
- AppImage packaging is still handled by CI (`ubuntu-latest`, a conventional
  base for AppImage builds) rather than locally for this release — building
  it on this development host's unusually new toolchain surfaced a low-level
  ELF/glibc incompatibility in the bundled Qt platform plugins unrelated to
  Athena's own code.

## [1.0] - earlier

Initial tagged release. See git history for details predating this changelog.
