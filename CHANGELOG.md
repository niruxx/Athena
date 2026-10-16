# Changelog

All notable changes to Athena are documented in this file. Newest release first;
append new entries above as changes land.

## [3.0.0] - 2026-10-14

### Added

- **Material You redesign.** A navigation rail (Home, Discover, Installed,
  Updates, Sources, Tools, Settings) replaces the top-right source dropdown
  and the per-source tab bars, beside a rounded content pane with a title
  bar (theme toggle and an overflow menu in place of the menu bar). The
  color scheme is generated from a single accent ("seed") color in light and
  dark, with Material Symbols icons bundled in the app.
  - **Home**: pending updates broken down by source with "Update
    everything", package-cache size and a left-behind-dependency cleanup,
    installed totals, AUR updates waiting for review, and recent activity.
  - **Discover**: one search across every source at once, results streaming
    in source by source. Apps are shown as cards, with the same app from
    different sources merged into one card (pacman's `gimp` and Flathub's
    GIMP); a Packages view lists everything, and chips filter by source.
  - **Installed** and **Updates**: everything installed, and every pending
    update grouped by source, across all sources, with real app names and
    icons from installed `.desktop` files.
  - **Details side sheet**: pick which source to get an app from, then
    install, remove, update, or open it; shows its description, version,
    size, repository, homepage, and — for Flatpak apps — a summary of its
    sandbox permissions, with risky ones (full filesystem, host commands,
    all devices) highlighted. AUR installs and updates still go through
    PKGBUILD review.
  - **Sources** holds each source's existing tools until they're migrated
    (repositories, groups, the classic package browser, advanced search,
    history, Flatpak permissions/user data/leftover data/backup, the AUR
    PKGBUILD viewer); **Tools** is the former User Management group. These
    tabs now load on first open instead of at startup.
- **Discover finds apps that aren't installed.** An index of every desktop
  app the sources offer, built in the background from the AppStream
  catalogs already on the system — Arch's `archlinux-appstream-data`,
  Fedora's catalogs, Debian/Ubuntu DEP-11 data, and each Flatpak remote's
  appstream (system and user installations); about 4,700 apps on a typical
  Arch + Flathub setup, loaded in under two seconds. Searches answer from the
  index instantly, then merge in each source's own search; with no search,
  category chips (Audio & Video, Development, Education, Games, Graphics,
  Internet, Office, Science, System, Utilities) browse it. Catalog icons and
  full descriptions show on cards and in the details sheet. Arch's catalog
  icons are JPEG XL and need `kimageformats`.
- **Athena's own title bar**, matching the design: the page title row and
  the navigation rail's empty space move the window (double-click to
  maximize), with minimize / maximize / close buttons and resizing from the
  window edges. Moving and resizing are handed to the compositor, so Wayland
  and X11 snapping and tiling still work. Settings → General → Appearance
  can switch back to the desktop's title bar.
- **Motion throughout the app**: pages, source chips, and every tab widget
  fade through when switched; the navigation rail's active indicator slides
  between destinations; every push button (new pages, classic pages, and
  dialogs) gets Material's state layer — a fading hover tint and a press
  ripple; list rows and app cards fade their hover; lists crossfade when
  they refresh; the snackbar slides up; the details sheet's scrim fades with
  its slide; dialogs fade in. Durations follow the desktop's animation speed
  (KDE's animation-speed slider, GNOME's "enable animations"), and
  Settings → General → Appearance → Animations turns it all off.
- **Material dialogs**: every dialog and message box — confirmations, the
  terminal output window (now with a progress indicator and a clear
  result), PKGBUILD review, Add Repository, errors and questions — is a
  rounded, shadowed Material card with a headline, supporting text, and
  right-aligned actions named after what they do ("Uninstall" rather than
  "Yes"). Under the Breeze look they're native dialogs with the same layout.
- **First-run setup is a five-step wizard** — Welcome, Look (Material You
  or Breeze as selectable cards, accent color, theme), Sources (Flatpak/Snap
  support, and on Arch the PKGBUILD review switch and AUR helper), a few
  Preferences, and a summary — with Back/Next, page dots, page transitions,
  and "Skip setup" at any step. Choices apply live, as in Settings.
- **Batch selection** in Installed, Updates, and Discover: "Select" adds
  check circles to rows and cards and a bar with Select all plus "Remove
  selected" / "Update selected" / "Install selected" and "Download"
  (with or without dependencies), across sources (AUR builds still go
  through PKGBUILD review).
- **Right-click menus** on every new list and card grid: Details, then
  Install (or "Install from…" when several sources offer the app), Update,
  Reinstall, Remove, and Select.
- **Reinstall** in the details sheet.
- **Advanced search in Discover**: Dependency Query (provides/requires) and
  Repository/Architecture filters, with full package details fetched for
  the filters while Advanced is open — the classic search's tools, across
  every source at once.
- "Start on page" can also open on Sources or Tools, and the ⋮ menu has a
  Repositories shortcut again.
- Settings → Layout: show architecture and size in package lists (also in
  the new lists, where the source reports them); compact rows now apply to
  the new lists too.
- Settings → General → Appearance: **Look** (Material You or Breeze) and
  **Accent color** (six seeds plus a custom hue). Breeze keeps the new
  layout with native widgets.
- **Arch news before updating.** Home shows a card for archlinux.org news
  posted since your last system upgrade, and "Update everything" stops first
  when there's unread news (Read first / Update anyway), so
  manual-intervention notices aren't missed.
- **PKGBUILD diffs for AUR updates.** Each build you approve is kept, and
  the review dialog's "Changes since you approved" view shows a colored diff
  of only what changed in each file since then, with changed/removed files
  counted per package.
- **Snapshots before transactions** (Settings → System): with Snapper or
  Timeshift set up, Athena takes a snapshot before installs, removals,
  reinstalls, downgrades, and updates (one per batch), and Home shows the
  latest one. Without either tool the option is off, with a note saying why.
- **Free up space**: Home's cleanup tile opens a dialog with the package
  cache's size and the matching cleanup (`paccache` or `pacman -Sc`,
  `apt autoclean`/`apt clean`, `dnf clean packages`), plus left-behind
  dependencies.
- **Export & Import** (Tools): saves the apps and packages you chose to
  install — from every source, without the dependencies they pulled in — to
  one JSON file, and on another machine lists what that file has that isn't
  installed yet, to pick and install (AUR packages still go through PKGBUILD
  review).
- **Downgrade on Arch**: "Other versions…" in the details sheet lists the
  versions in the local pacman cache and the Arch Linux Archive, newest
  first, and installs the one you pick.
- **Mirrors** (Sources → pacman/dnf): ranks pacman mirrors by speed with
  `reflector` (or `rate-mirrors`), offering to install reflector when it's
  missing, and shows the new list before it replaces the old one (which is
  backed up); on DNF it turns on `fastestmirror`.
- **Screenshots in the details sheet**, from the AppStream catalogs, fetched
  once and cached.
- **AppStream for installed apps and updates**: Installed and Updates use
  the catalog's names, icons, and descriptions as well as installed
  `.desktop` files.
- **Package groups in Discover**: a "Package groups" chip with a group
  picker (comps groups, tasksel tasks, pacman groups) beside the categories.
- **"Edit permissions"** in a Flatpak app's details sheet, going straight to
  that app in Sources → Flatpak → Permissions.
- **Activity timeline**: Home's recent activity and the History tab's new
  Timeline view (beside the raw log) read pacman's log, APT's history, DNF
  history, and Flatpak history into one list of installs, removals, and
  upgrades.
- **Bottom navigation bar** in narrow windows (under 720px wide), as
  Material 3 does; the window can now shrink to 420×520.

- **Snap management** (Sources → Snap → Manage): for each installed snap,
  switch the channel it follows (track and risk level, from the store's open
  channels), revert to the revision kept from before its last refresh,
  enable or disable it, and connect or disconnect its interfaces with
  switches, the Snap counterpart of Flatpak's permissions. Classic-confinement
  snaps say interfaces don't apply to them.
- **Snap in the activity timeline**: installs, removals, refreshes
  (including auto-refreshes of several snaps at once), and reverts from
  `snap changes`, on Home and in Sources → Snap → History.

- **Snapshots page** (Tools → Snapshots, and Home's Snapshots tile): every
  Snapper or Timeshift snapshot, newest first, with what it was for
  (Snapper's before/after pairs, Timeshift's on-demand/boot/daily tags),
  when, and who took it. Take one with a description, delete one, or
  restore one. With Snapper, choose between rolling back at the next
  restart and undoing the changes made since, right away; Timeshift
  restores the files and restarts, leaving the bootloader alone. Timeshift
  (and Snapper configs that don't allow your account) list snapshots only
  with administrator access, behind a "Show snapshots" button rather than a
  password prompt on opening the tab.

### Changed

- "Start on tab" is now "Start on page" (Home, Discover, Installed, or
  Updates). The previous setting doesn't carry over, so Athena starts on
  Home until you pick another page.
- Material You with "Follow System" follows the desktop's light/dark
  preference live.
- Installed/Updates/Home share one background load of every source instead
  of each page querying every backend separately.
- The classic pages under Sources and Tools use the new design's buttons
  (filled/tonal/destructive roles with Material icons), cards, and search
  field; the Package Browser shows its listed/installed/to-install/to-remove
  counts (the old status bar's) under its table.
- Flatpak and Snap have their **Advanced Search** tabs again (Dependency
  Query, and downloading without installing).
- The unused Preferences dialog and the old per-source Updates tab were
  removed (Settings is a page now; the cross-source Updates page covers the
  tab, including updating a hand-picked selection).
- Repositories are a card of rows with on/off switches instead of a table
  with checkboxes.
- Open in the details sheet starts native apps from their `.desktop` file
  directly, so it no longer needs `gio`.
- Dialogs scale in as well as fading in, on Wayland too (the animation is
  drawn inside the dialog rather than relying on window opacity).
- Discover's category chips scroll sideways when the window is too narrow
  for them, instead of squeezing their labels.
- **The classic pages are redesigned** in the new style:
  - Flatpak Permissions is a set of cards with a switch per permission,
    each saying what it allows, a header with the app's icon, and list cards
    for folders, D-Bus services, and environment variables; its dialogs are
    Material dialogs.
  - User Data, Leftover Data, both cleanup tabs, the Flatpak and dotfile
    backup lists, and the Groups and Package Browser sidebars are Material
    lists: app icons or symbol tiles, a supporting line (app ID and folder,
    what a cleanup target is), and sizes on the right, largest first.
  - Backup & Restore (Flatpak and Tools) and the PKGBUILD viewer use
    section cards with icon headers; Advanced Search's filters are one card.
  - The Package Browser is a borderless data table with wider version
    columns, and its counts sit under the table instead of being cut off
    beside the buttons.
  - Checkboxes and radio buttons are drawn as Material 3's everywhere under
    the Material look.
- **Flatpak has its own page** in the navigation rail, with Permissions,
  App data, Leftovers, and Backup as segmented sections; Sources → Flatpak
  keeps the source's own tabs (Remotes, Package Browser, Advanced Search,
  History). "Edit permissions" in the details sheet opens the new page.
- The navigation rail's destinations sit closer together, so all eight fit
  in a short window.

### Fixed

- **Updates had no progress.** Updating (one app, a source's "Update all",
  or "Update everything") ran silently in the background, so the Updates
  page sat unchanged until it finished — or until a long Flatpak update hit
  a ten-minute timeout. Updates now run in the terminal window like
  installs, with a progress bar and a line saying what's happening, read
  from each tool's output: pacman's and dnf's step counts, APT's
  percentage, the app Flatpak or Snap is on. Installs and removals get the
  same progress. "Update everything" is a real full upgrade per source
  (`pacman -Syu`, `dnf upgrade`, `apt-get upgrade`, `flatpak update`,
  `snap refresh`) instead of a list of package names, so on Arch it's no
  longer a partial upgrade.
- **No password prompt on some desktops.** pkexec needs a polkit
  authentication agent running in the session; bare compositors (niri,
  Hyprland, sway) and minimal installs often don't start one, and pkexec
  then printed its text-mode prompt into the terminal window and failed
  ("Error creating textual authentication agent … /dev/tty"). Athena now
  runs pkexec with its text prompt off, starts an installed agent itself
  when none is running, and explains plainly when there's no agent or the
  prompt was dismissed. The packages depend on an agent: `polkit-kde-agent`
  on Arch, any agent on Fedora (`polkit-kde` if there's none) and Debian
  (`polkit-kde-agent-1` or any `polkit-1-auth-agent`), and on Debian on
  `pkexec` itself, which newer Debian and Ubuntu ship separately.
- Tools → Backup & Restore showed "0 B" for every dotfile that's a plain
  file (`.bashrc`, `.bash_history`), so the selected total left them out.
- Two AUR messages still pointed to "Preferences → General → AUR"; they now
  say Settings.
- **AUR packages were invisible in the new pages.** Discover's Apps view
  dropped every AUR search result (the AUR has no app catalog to mark them
  as apps), and Installed's "Apps only" hid installed AUR packages without a
  `.desktop` file. AUR packages you installed now always count as apps
  (except `-debug` symbol packages); AUR search results whose name is, or
  starts with, the search show as cards, and all of them do with the AUR
  chip selected; AUR names merge with the same app from other sources
  (`brave-bin` with Flathub's Brave, `visual-studio-code-bin` with Visual
  Studio Code). Installed also says how many packages "Apps only" is hiding,
  with a "Show all" button.
- "Backup & Restore" tab labels showed as "Backup _Restore".

## [2.2.0] - 2026-10-09

### Added

- **AUR support (Arch Linux)**: a new **AUR** entry in the top-right
  dropdown, shown when the system backend is pacman, with Installed (foreign
  packages), Updates (AUR version vs. installed, via `vercmp`), Search (by
  name/description, plus Dependency Query for provides/depends), PKGBUILD,
  and History tabs. Metadata comes from the AUR RPC v5 API (fetched with
  `curl`, which falls back to IPv4 promptly on networks with a broken IPv6
  route). Athena resolves AUR-only dependencies itself and builds them
  first (installed `--asdeps`); repository dependencies are installed by
  makepkg through `pkexec`. With PKGBUILD review on (the default, see
  below), every build goes through the review dialog; with it off, builds
  go to `paru` or `yay` if installed (told to use `pkexec` instead of
  `sudo`), or else to a built-in `git clone` + `makepkg -si` fallback, and
  the install confirmation lists everything that will be built along with
  each package's votes, maintainer, and out-of-date/orphan status. The
  PKGBUILD tab shows any package's build script on demand, and "Download"
  clones a package's AUR git repo into the chosen folder. Uninstalling and
  "Clean Left Behind Dependencies" go through pacman as usual.
- The AUR group can be chosen as the **startup tab** (Preferences →
  General, and the first-run dialog), and the tray icon's update check now
  includes AUR updates.
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
- **AUR helper installer** in Preferences → General → AUR (Arch only): pick
  `yay` or `paru` and Athena builds it from its official AUR git repo with
  `makepkg -si`, installing `git`/`base-devel` first if needed. With
  PKGBUILD review on, the helper's own build files go through the review
  dialog like any other AUR package. Since a reviewed build has to build
  exactly the reviewed copy, Athena builds those with makepkg itself; the
  helper is used whenever review is turned off, with no restart needed.
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
- The Arch package (`packaging/arch/PKGBUILD`) lists `git`, `base-devel`,
  and `paru` as optional dependencies for AUR support.
- On Arch, the disabled "Install Snap Support" button's tooltip now points
  to installing `snapd` from the AUR tab instead of telling you to install
  an AUR helper by hand.
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
