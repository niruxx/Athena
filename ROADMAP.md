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

- [ ] **Settings** (GSettings schema `io.github.niruxx.Athena`, AdwPreferencesDialog):
  - [x] Appearance (follow system / light / dark), through AdwStyleManager
  - [ ] Start on page
  - [ ] Close the terminal window automatically on success
  - [ ] Skip confirmations ("Run transactions without asking")
  - [ ] Package-list options: show architecture and size, compact rows
  - [ ] DNF metadata expiry (hours)
  - [ ] Hide the Groups page
  - [ ] Check for Athena updates on startup
  - [ ] Logging to a folder, with a level
  - [ ] Flatpak / Snap support: install or remove (`distro::flatpak_install_commands` etc. exist)
  - [ ] Run first-time setup again
- [ ] **Details page** (AdwNavigationView page pushed from any list):
  - [ ] Source picker when several sources offer the same app
  - [ ] Install, Remove, Update, Reinstall, Open (launch from its .desktop file)
  - [ ] Summary, full description, version, size, repository, homepage
  - [ ] Screenshots, fetched once and cached
  - [ ] Flatpak: sandbox summary with risky permissions highlighted, and "Edit Permissions"
  - [ ] pacman: "Other Versions…" (downgrade from the cache or the Arch Linux Archive)
- [ ] **Selection mode** on Search, Installed, and Updates (batch install, remove, update, download)
- [ ] **Right-click menus** on list rows: Details, Install / Install from…, Update, Reinstall, Remove, Select
- [ ] **Download without installing:** folder picker (GtkFileDialog), with or without dependencies
- [ ] **Installed:**
  - [ ] "Apps only" toggle, with a "Show all" count of hidden packages
  - [ ] Real app names and icons from installed .desktop files
- [ ] **Groups page:**
  - [ ] Searchable group list, with installed groups marked
  - [ ] Per group: description, "N of M installed", Install missing, Remove installed
  - [ ] All / Installed / Not installed filter and search within the group
- [ ] **History:** per-source raw log, and the cross-source timeline (pacman.log, APT history, dnf history, Flatpak history, `snap changes`)
- [ ] **First-run setup** (AdwNavigationView or AdwCarousel):
  - [ ] Welcome
  - [ ] Sources (Flatpak/Snap support, and on Arch the AUR)
  - [ ] Preferences
  - [ ] Summary
- [ ] **Keyboard shortcuts window**
- [ ] **Release notices:** GitHub release check, with an AdwBanner when a newer version is out

## Phase 3: Discover and the app catalog

- [ ] **AppStream index:** distro catalogs (Arch `archlinux-appstream-data`, Fedora, Debian/Ubuntu DEP-11) and every Flatpak remote's appstream (system and user), gzip, JPEG XL icons (needs `kimageformats` or a JXL loader for GdkPixbuf). Evaluate libappstream through GObject introspection against Athena's own parser.
- [ ] **Discover page:**
  - [ ] All apps (the whole catalog)
  - [ ] Categories (Audio & Video, Development, Education, Games, Graphics, Internet, Office, Science, System, Utilities)
  - [ ] App cards in a grid (GtkGridView), with one card per app across sources (`firefox`, Flathub's Firefox and `firefox-bin` merged)
  - [ ] Apps / Packages views
  - [ ] Advanced: Dependency Query, repository and architecture filters
- [ ] Catalog names, icons and descriptions on Installed and Updates too

## Phase 4: Arch and the AUR

- [ ] **AUR backend:**
  - [ ] RPC v5 search and info
  - [ ] Installed foreign packages, and updates through `vercmp`
  - [ ] AUR-only dependency resolution (built first, installed `--asdeps`)
  - [ ] Builds through paru or yay, or the built-in `git clone` + `makepkg` fallback (`PACMAN_AUTH=pkexec`)
- [ ] **PKGBUILD review**, on by default and failing closed:
  - [ ] Every build file is shown and editable, risky lines are flagged, and exactly the reviewed copy is built
  - [ ] Approved copies are kept, and updates open on a diff against them
- [ ] **AUR helper install and removal** (yay or paru, from its AUR git repository with `makepkg -si`)
- [ ] **PKGBUILD viewer** (read any package's build files)
- [ ] **AUR results in Search and Discover** ranked by relevance, and AUR updates going to review after "Update Everything"
- [ ] **Arch news:**
  - [ ] Unread archlinux.org posts on Home
  - [ ] Updating everything stops for unread news (Read first / Update anyway)
- [ ] **Mirror ranking:** reflector or rate-mirrors, with a preview before saving and a backup; DNF's fastestmirror

## Phase 5: Flatpak and Snap tools

- [ ] **Flatpak page:**
  - [ ] Permissions editor: switch rows per permission with explanations; list cards for folders, D-Bus names and environment variables; Reset
  - [ ] App data (`~/.var/app` per app, sizes, open, clear)
  - [ ] Leftovers (data and overrides of uninstalled apps)
- [ ] **Snap Manage page:**
  - [ ] Switch channel
  - [ ] Revert to the previous revision
  - [ ] Enable/disable
  - [ ] Connect and disconnect interfaces
  - [ ] Classic-confinement note
- [ ] **Deep link:** "Edit Permissions" in Details opens the app in the Flatpak page

## Phase 6: Tools and Home

- [ ] **Home dashboard:**
  - [ ] Pending updates by source, with Update Everything
  - [ ] Package cache size, with Free Up Space
  - [ ] Installed totals
  - [ ] AUR updates waiting for review
  - [ ] Latest snapshot
  - [ ] Arch news
  - [ ] Recent activity, with "See All"
- [ ] **Free up space:**
  - [ ] Package cache (`paccache` / `pacman -Sc`, `apt clean`, `dnf clean`) and unused dependencies, with sizes first
  - [ ] Your files, everyday (`/tmp` files you own, `~/.cache`) and worth a second look (trash, thumbnails, shell history, recent files)
- [ ] **Backup & restore:**
  - [ ] Dotfiles and config (compressed or plain copy), restore with a preview
  - [ ] Flatpak app data and app list
- [ ] **Export & import:**
  - [ ] Everything you chose to install, from every source, to one JSON file
  - [ ] Import shows what's missing to pick from, with AUR packages going through review
- [ ] **Snapshots** (Snapper or Timeshift):
  - [ ] A snapshot before each transaction (setting), one per batch
  - [ ] Browse, take, delete
  - [ ] Restore: Snapper rollback or undo changes, Timeshift restore
  - [ ] Timeshift lists only behind an administrator button

## Phase 7: Desktop integration and release

- [ ] **Background update checks** on an interval, with a notification (GNotification). GNOME has no tray, so: the Background portal for "run in background", plus a StatusNotifierItem tray icon where the desktop supports one (KDE, others), with Update and Quit.
- [ ] **AppStream metainfo** (`io.github.niruxx.Athena.metainfo.xml`) with screenshots, for software centers
- [ ] **Translations** (gettext)
- [ ] **New README screenshots** (the debug build saves them: `ATHENA_SCREENSHOTS=<dir>`)
- [ ] **4.0.0 release** once phases 2–6 are done; the Qt version is fully replaced
- [ ] **Distribution formats:**
  - [ ] AppImage (CI built one for Qt; GTK AppImages need care with the bundled GTK)
  - [ ] Flathub: Athena drives the host's package managers, so a Flatpak build would need `flatpak-spawn --host` and Flathub's approval for that

## Dropped on purpose

- **Material You theming, the accent picker, Breeze look, the custom title bar, the motion system:** libadwaita provides the look, the system accent, client-side decorations and its own animations, and honors the desktop's animation setting.
- **The Synaptic-style package table:** Installed with selection mode covers it (as in 3.0's last layout pass).
