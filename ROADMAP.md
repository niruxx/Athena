# Athena roadmap: Rust + GTK rewrite

Athena is being rewritten from C++/Qt to **Rust with GTK 4 and libadwaita**. This file tracks what the new version needs to do everything the Qt version did (the last Qt release is **3.0.0**, recoverable from git history at commit `b169b76`), in the order it's planned.

`[x]` done · `[~]` partly done (what's missing is listed under it) · `[ ]` not started

## Decisions

- **Look:** libadwaita, following the GNOME Human Interface Guidelines. It uses the system's light/dark preference and accent color, so the Qt version's own theming goes away: Material You, the seed accent, the Breeze look switch, the custom title bar, the animation system, and the Material Symbols font. Icons come from the icon theme (symbolic icons).
- **Minimum versions:** GTK 4.14 and libadwaita 1.5 (Ubuntu 24.04 LTS). Debian 13, Fedora 40+ and Arch are newer. CI builds against Ubuntu 24.04 to hold that line.
- **Layout:** a Cargo workspace with two crates.
  - `crates/athena-core` has no GTK. It holds the backends, process running, polkit handling and output parsers, all unit-tested.
  - `crates/athena` is the libadwaita app.
- **Privileges:** unchanged from 3.0. Changes run through `pkexec --disable-internal-agent`, and Athena starts an installed polkit agent when the session has none. Flatpak uses its own polkit integration.
- **App ID:** `io.github.niruxx.Athena`.

## Phase 1: Foundation (`4.0.0-alpha.1`)

The core and a usable app: search, install, remove and update across every source.

- [x] Cargo workspace, release profile, and `cargo test` suite for the parsers
- [x] Process running: timeouts, sequences, stdin, `LC_ALL=C`, pkexec's text prompt turned off
- [x] Polkit agent detection, and starting one when the session has none
- [x] Distribution detection and backend selection (native, then Flatpak and Snap)
- [x] Backends: **pacman**, **DNF**, **APT**, **Flatpak**, **Snap**
  - [x] Installed, search, package details, updates
  - [x] Install, remove (with unused dependencies), reinstall, update and full update commands
  - [x] Transaction previews (resolved plan)
  - [x] Clean unused dependencies, download-only commands
  - [x] Package groups (pacman groups, dnf groups and environments, tasksel)
  - [x] Repositories: list, switch on/off, add (including COPR), refresh metadata
  - [x] Flatpak sandbox permissions API (read, every override, reset)
  - [x] Raw history logs
- [x] Progress parsing (pacman, dnf4/5, APT Status-Fd, Flatpak, Snap) and plain-language pkexec failures
- [x] Window: sidebar split view that collapses to one pane below 600sp, primary menu, About, Ctrl+F / Ctrl+R / Ctrl+Q
- [x] **Search:** every source at once, results as they arrive, ranked by name match, source filter
- [x] **Installed:** everything from every source, text and source filters, Reinstall and Remove per package
- [x] **Updates:** grouped by source, update one, "Update All" per source (its full update), "Update Everything", count badge in the sidebar
- [x] **Repositories:** switch rows per repository or remote, "Add…", "Update Package Lists"
- [x] Confirmation dialog with the dependency plan, and the terminal dialog: live log, progress bar, current step, plain result
- [x] Packaging: PKGBUILD, Debian and Fedora podman builds, desktop entry and icons, CI (tests on the minimum GTK, clippy, all three packages)

## Phase 2: Feature parity, everyday use

- [x] **Settings** (GSettings schema `io.github.niruxx.Athena`, AdwPreferencesDialog):
  - [x] Appearance (follow system / light / dark), through AdwStyleManager
  - [x] Start on page
  - [x] Close the terminal window automatically on success
  - [x] Skip confirmations ("Run transactions without asking")
  - [x] Package-list options: show architecture and size, compact rows
  - [x] DNF metadata expiry (hours)
  - [x] Hide the Groups page
  - [x] Check for Athena updates on startup
  - [x] Logging to a folder, with a level
  - [x] Flatpak / Snap support: install or remove (`distro::flatpak_install_commands` etc. exist)
  - [x] Run first-time setup again
- [x] **Details page** (AdwNavigationView page pushed from any list):
  - [x] Source picker when several sources offer the same app
  - [x] Install, Remove, Update, Reinstall, Open (launch from its .desktop file)
  - [x] Summary, full description, version, size, repository, homepage
  - [x] Screenshots from available AppStream metadata, fetched once and cached (the complete catalog is Phase 3)
  - [x] Flatpak: sandbox summary with risky permissions highlighted, and "Edit Permissions"
  - [x] pacman: "Other Versions…" (downgrade from the cache or the Arch Linux Archive)
- [x] **Selection mode** on Search, Installed, and Updates (batch install, remove, update, download)
- [x] **Right-click menus** on list rows: Details, Install / Install from…, Update, Reinstall, Remove, Select
- [x] **Download without installing:** folder picker (GtkFileDialog), with or without dependencies
- [x] **Installed:**
  - [x] "Apps only" toggle, with a "Show all" count of hidden packages
  - [x] Real app names and icons from installed .desktop files
- [x] **Groups page:**
  - [x] Searchable group list, with installed groups marked
  - [x] Per group: description, "N of M installed", Install missing, Remove installed
  - [x] All / Installed / Not installed filter and search within the group
- [x] **History:** per-source raw log, and the cross-source timeline (pacman.log, APT history, dnf history, Flatpak history, `snap changes`)
- [x] **First-run setup** (AdwNavigationView or AdwCarousel):
  - [x] Welcome
  - [x] Sources (Flatpak/Snap support, and an AUR opt-in on Arch)
  - [x] Preferences
  - [x] Summary
- [x] **Keyboard shortcuts window**
- [x] **Release notices:** GitHub release check, with an AdwBanner when a newer version is out

## Phase 3: Discover and the app catalog

- [x] **AppStream index:** distro catalogs (Arch `archlinux-appstream-data`, Fedora, Debian/Ubuntu DEP-11) and every Flatpak remote's appstream (system and user), gzip and JPEG XL icons (GdkPixbuf loader or cached `djxl` conversion). The own-parser/libappstream choice is documented in [catalog-and-aur.md](docs/catalog-and-aur.md).
- [x] **Discover page:**
  - [x] All apps (the whole catalog)
  - [x] Categories (Audio & Video, Development, Education, Games, Graphics, Internet, Office, Science, System, Utilities)
  - [x] App cards in a grid (GtkGridView), with one card per app across sources (`firefox`, Flathub's Firefox and `firefox-bin` merged)
  - [x] Apps / Packages views
  - [x] Advanced: Dependency Query, repository and architecture filters
- [x] Catalog names, icons and descriptions on Installed and Updates too

## Phase 4: Arch and the AUR

- [x] **AUR backend:**
  - [x] RPC v5 search and info
  - [x] Installed foreign packages, and updates through `vercmp`
  - [x] AUR-only dependency resolution (built first, installed `--asdeps`)
  - [x] Builds through the reviewed local `git clone` + `makepkg` executor (`PACMAN_AUTH=pkexec`); helpers cannot refetch a different copy after approval
- [x] **PKGBUILD review**, on by default and failing closed:
  - [x] Every build file is shown and editable, risky lines are flagged, and exactly the reviewed copy is built
  - [x] Approved copies are kept, and updates open on a diff against them
- [x] **AUR helper install and removal** (yay or paru, from its AUR git repository with `makepkg -si`)
- [x] **PKGBUILD viewer** (read any package's build files)
- [x] **AUR results in Search and Discover** ranked by relevance, and AUR updates going to review after "Update Everything"
- [x] **Arch news:**
  - [x] Unread archlinux.org posts on Home
  - [x] Updating everything stops for unread news (Read first / Update anyway)
- [x] **Mirror ranking:** reflector or rate-mirrors, with a preview before saving and a backup; DNF's fastestmirror

## Phase 5: Flatpak and Snap tools

- [x] **Flatpak page:**
  - [x] Permissions editor: switch rows per permission with explanations; list cards for folders, D-Bus names and environment variables; Reset
  - [x] App data (`~/.var/app` per app, sizes, open, clear)
  - [x] Leftovers (data and overrides of uninstalled apps)
- [x] **Snap Manage page:**
  - [x] Switch channel
  - [x] Revert to the previous revision
  - [x] Enable/disable
  - [x] Connect and disconnect interfaces
  - [x] Classic-confinement note
- [x] **Deep link:** "Edit Permissions" in Details opens the app in the Flatpak page

## Phase 6: Tools and Home

- [x] **Home dashboard:**
  - [x] Pending updates by source, with Update Everything
  - [x] Package cache size, with Free Up Space
  - [x] Installed totals
  - [x] AUR updates waiting for review
  - [x] Latest snapshot
  - [x] Arch news
  - [x] Recent activity, with "See All"
- [x] **Free up space:**
  - [x] Package cache (`paccache` / `pacman -Sc`, `apt clean`, `dnf clean`) and unused dependencies, with sizes first
  - [x] Your files, everyday (`/tmp` files you own, `~/.cache`) and worth a second look (trash, thumbnails, shell history, recent files)
- [x] **Backup & restore:**
  - [x] Dotfiles and config (compressed or plain copy), restore with a preview
  - [x] Flatpak app data and app list
- [x] **Export & import:**
  - [x] Everything you chose to install, from every source, to one JSON file
  - [x] Import shows what's missing to pick from, with AUR packages going through review
- [x] **Snapshots** (Snapper or Timeshift):
  - [x] A snapshot before each transaction (setting), one per batch
  - [x] Browse, take, delete
  - [x] Restore: Snapper rollback or undo changes, Timeshift restore wizard
  - [x] Timeshift lists only behind an administrator button

Implementation and verification notes: [Tools and recovery](docs/tools-and-recovery.md).

## Phase 7: Desktop integration and release

- [x] **Background update checks** on an interval, with a notification (GNotification). Background portal consent and login startup; StatusNotifierItem/DBusMenu on supported desktops, with Open, Update and Quit. Portal/tray protocol verified on a private bus.
- [x] **AppStream metainfo** (`io.github.niruxx.Athena.metainfo.xml`) with screenshots, for software centers
- [x] **Translations** (gettext infrastructure, extraction/install tooling, Spanish starter catalog; further translation coverage is welcome)
- [x] **New README screenshots** (debug harness, wide/narrow GTK pages)
- [ ] **4.0.0 publication** after CI/distribution verification; local version, changelog, metadata and release build are prepared. No release/tag has been published.
- [ ] **Distribution formats:**
  - [ ] AppImage: pinned GTK4 build recipe and Ubuntu 24.04 CI job prepared. Local Arch build hit the upstream plugin's Glycin/module-directory incompatibility; the baseline CI artifact still needs validation.
  - [ ] Flathub: host-companion integration and permission review remain. [Readiness assessment](packaging/flatpak/README.md).

Implementation, checks and publication steps: [Desktop integration and releasing](docs/releasing.md).

## Dropped on purpose

- **Material You theming, the accent picker, Breeze look, the custom title bar, the motion system:** libadwaita provides the look, the system accent, client-side decorations and its own animations, and honors the desktop's animation setting.
- **The Synaptic-style package table:** Installed with selection mode covers it (as in 3.0's last layout pass).
