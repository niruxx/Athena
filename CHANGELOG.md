# Changelog

All notable changes to Athena are documented in this file. Newest release first;
append new entries above as changes land.

## [Unreleased]

## [2.2.0] - 2026-10-09

### Added

- **AUR support (Arch Linux)**: a new **AUR** entry in the top-right
  dropdown, shown when the system backend is pacman, with Installed (foreign
  packages), Updates (AUR version vs. installed, via `vercmp`), Search (by
  name/description, plus Dependency Query for provides/depends), PKGBUILD,
  and History tabs. Metadata comes from the AUR RPC v5 API. Packages are
  built with `paru` or `yay` when installed (told to use `pkexec` instead of
  `sudo`), or else with a built-in `git clone` + `makepkg -si` fallback that
  also resolves and builds AUR-only dependencies first (installed
  `--asdeps`). The install confirmation lists everything that will be built
  along with each package's votes, maintainer, and out-of-date/orphan
  status. The PKGBUILD tab shows a package's build script before you
  install it. "Download" clones the package's AUR git repo into the chosen
  folder.
- **PKGBUILD review before every AUR build** (on by default): installing,
  reinstalling, or updating AUR packages — including from the tray's
  "Update" action and the AUR helper installer — first downloads each
  package's build files, AUR dependencies included, and opens them in a
  review dialog. Every file (PKGBUILD, `.install` scriptlets, patches) can
  be read and edited, lines worth a closer look (piping downloads into a
  shell, `sudo`, `eval`, base64 decoding, plain-HTTP sources, skipped
  checksums, edits to shell startup files/sudoers/cron...) are flagged and
  jump to the line, and symlinks/binaries/subdirectories are called out
  rather than hidden. "Build & Install" stays disabled until every package
  has been opened and you confirm you trust it, and exactly the reviewed
  (and possibly edited) copies are built with makepkg — nothing is
  re-downloaded in between. "Run transactions without confirmation"
  doesn't skip it, and while review is on, every unreviewed build path
  refuses to run instead of silently building. It can be turned off (with
  a warning) in Preferences → General → AUR.
- **AUR helper installer** in Preferences → General (Arch only): pick `yay`
  or `paru` and Athena clones it from its official AUR git repo and runs
  `makepkg -si` for it, installing `git`/`base-devel` first if needed. The
  AUR tab switches to the new helper for its next build, with no restart.
  An **Uninstall** button next to it removes the installed helper again
  (whichever package provides it — `yay`, `yay-bin`, `paru-git`...), via
  `pacman -Rs`.
- **User Management group**: a new entry in the top-right dropdown, alongside
  System/Flatpak/Snap, for housekeeping that isn't about packages at all —
  General (clear `/tmp` and `~/.cache`) and Advanced (trash, thumbnail cache,
  shell/tool history, recently-used files list) tabs show a size for each
  item before you delete it, and only ever remove files you own (so pointing
  it at a shared `/tmp` is safe). Its Backup & Restore tab archives your
  dotfiles/configuration (hidden top-level entries under your home
  directory) to either a `.tar.gz` or a plain uncompressed copy — your
  choice — and restores either kind back, for moving to a new machine or a
  fresh OS install.

### Changed

- Version bumped to 2.2.0.
- Sidebars are separated from the content beside them by a thin divider
  line with proper spacing on both sides, instead of sitting right against
  the filter field and package list, and empty-state hints ("Select a
  package to see its details.") are centered.
- **Breeze-style refresh, modeled on KDE Discover**: roomier layout margins
  and spacing, taller buttons/fields/tabs/list rows, flat document-mode tab
  bars, a grid-less package table, and a larger details-panel title. The
  left-hand lists (Installed categories, Groups, Flatpak Permissions apps)
  are now borderless sidebars with icons. Forced Light/Dark themes now use
  the Breeze style and KDE's own Breeze Light/Breeze Dark colors (falling
  back to Fusion where Breeze isn't installed), including the menu bar
  area and Breeze's light/dark icon variants. "Follow System" now keeps
  your desktop's own color scheme instead of resetting to the style's
  default.
- The left-hand lists are sorted alphabetically (case-insensitive), with
  entries like "All Installed Packages" kept pinned at the top.

## [2.1.0] - 2026-09-06

### Added

- **Flatpak permission editor**: a new Permissions tab under the Flatpak
  group lets you inspect and edit an installed app's sandbox access —
  shared namespaces (network/IPC), sockets, devices, sandbox features
  (devel/multiarch/bluetooth/canbus/etc.), filesystem access, D-Bus name
  policies, and environment variables — all backed by `flatpak override
  --user`, so changes apply immediately and need no elevated privileges.
- **Flatpak User Data tab**: browse and clear `~/.var/app/<id>` data for
  currently installed apps, with recursive size scanning.
- **Flatpak Leftover Data tab**: find (and delete) data left behind under
  `~/.var/app`, plus orphaned permission-override files, from apps
  uninstalled outside Athena.
- **Flatpak Backup & Restore tab**: back up all Flatpak user data and the
  list of installed apps to a single `.tar.gz`; restore the data or
  reinstall the listed apps on a new machine or after a fresh OS install.
- **COPR support** in the DNF Repositories tab's "Add Repository" dialog —
  enter an `owner/project` COPR repo to enable it via `dnf copr enable`,
  alongside the existing custom repository ID/URL fields.
- **Nightly CI build**: the GitHub Actions build workflow now also runs on
  a nightly schedule, not just on push/PR/manual dispatch.
- The first-run setup dialog now lets you configure theme, startup tab,
  compact package lists, and update-check-on-startup right there, instead
  of only offering the Flatpak/Snap install prompt.
- A **"Run First-Time Setup Again..."** button in Preferences → General,
  for revisiting the first-run dialog's choices without reinstalling.

### Fixed

- The Repository/Flatpak/Snap group switcher (top-right of the window)
  could become invisible and unusable after switching groups more than
  once, due to a Qt quirk where `QTabWidget::setCornerWidget()` silently
  no-ops if handed a widget it already believes is its corner widget.
- **Pacman backend**: the Groups tab always showed "0 categories" — 
  `listGroups()` was parsing bare `pacman -Sg` output (one group name per
  line) as if it were the `pacman -Sg <name>` pairs format, so every
  group was dropped. Fixed, and the per-group "installed?" check is now a
  single batched `pacman -Qg` call instead of one subprocess per group.

### Changed

- Version bumped to 2.1.0.

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
