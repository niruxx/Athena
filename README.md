<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/banner/dark.png">
  <img alt="Athena — one home for every Linux package" src="docs/banner/light.png" width="100%">
</picture>

### 📦 A cross-distro package manager for Linux, built with Qt6 and dressed in Material You.

Your system's packages (**DNF**, **APT**, or **pacman**), the **AUR**, **Flatpak**, and **Snap**: one search, one Updates page, one window.

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-4A6820.svg?style=for-the-badge)](LICENSE)
[![Build packages](https://img.shields.io/github/actions/workflow/status/niruxx/Athena/c-cpp.yml?style=for-the-badge&label=build&color=386663)](https://github.com/niruxx/Athena/actions/workflows/c-cpp.yml)
![Qt6](https://img.shields.io/badge/Qt-6-41cd52.svg?style=for-the-badge&logo=qt&logoColor=white)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg?style=for-the-badge&logo=cplusplus&logoColor=white)
![Platform](https://img.shields.io/badge/platform-Linux-5C6152.svg?style=for-the-badge&logo=linux&logoColor=white)

[✨ Features](#-features) · [📸 Screenshots](#-screenshots) · [🐧 Supported systems](#-supported-systems) · [📥 Installing](#-installing) · [🔨 Building](#-building-from-source) · [🔐 Privileges](#-a-note-on-privileges) · [🧭 Roadmap](#-roadmap)

<br>

<img alt="Athena's Home page" src="docs/screenshots/m3-home.png" width="88%">

</div>

<br>

## ✨ Features

### 🎨 Material You, in Qt Widgets

<table>
<tr>
<td width="50%" valign="top">

- 🌈 **One accent, a whole scheme.** The color scheme is generated from a single seed color, in light and dark. Pick one of six or slide to any hue.
- 🧭 **Navigation rail**: Home, Discover, Installed, Updates, Sources, Tools, Settings. Narrow windows get Material's bottom navigation bar instead.
- 🪟 **Athena's own title bar.** Drag, double-click to maximize, resize from the edges. Snapping and tiling still work on Wayland and X11.

</td>
<td width="50%" valign="top">

- 💫 **Motion throughout**: page fades, a sliding rail indicator, ripples and hover tints, dialogs that scale in, a sliding snackbar. It follows your desktop's animation speed and can be turned off.
- 🖌️ **Prefer KDE's look?** Switch **Look** to **Breeze**. The layout stays and the widgets go native.
- 👋 **A five-step first-run wizard** for your look, sources, and preferences.

</td>
</tr>
</table>

### 🔎 Find and install anything

- 🛍️ **Discover finds apps you don't have yet.** It builds an index from the AppStream catalogs already on your system (Arch, Fedora, Debian/Ubuntu, and every Flatpak remote), about 4,700 apps loaded in under two seconds. Browse by category or package group, or search every source at once.
- 🧩 **One card per app.** pacman's `firefox`, Flathub's Firefox, and the AUR's `firefox-bin` merge into one card; pick the source in the details sheet.
- 📋 **Details sheet** with screenshots, description, version, size, homepage, and a summary of a Flatpak app's sandbox permissions (risky ones highlighted), plus Install, Remove, Update, Reinstall, Open, and **Edit permissions**.
- ☑️ **Batch selection** in Discover, Installed, and Updates, and **right-click menus** on every list and card.
- 🔍 **Advanced search** in Discover: Dependency Query (what provides or requires a capability), plus Repository and Architecture filters.
- ⬇️ **Download without installing**, with or without dependencies.

### ⬆️ Updates you can trust

- 🔄 **One Updates page** across every source, grouped by source: "Update everything" or a hand-picked selection.
- 📰 **Arch news first.** Unread [archlinux.org news](https://archlinux.org/news/) shows on Home and stops a system update until you've read it or chosen to go ahead.
- 📸 **Snapshots before changes.** With Snapper or Timeshift, Athena takes a snapshot before each install, removal, downgrade, and update (**Settings → System**), and **Tools → Snapshots** lists them all to take, delete, or restore one: a Snapper rollback at the next restart, undoing the changes since in place, or a Timeshift restore.
- 🔍 **See the real plan first.** Confirmations show the dependency-resolved transaction: what gets pulled in and what gets cleaned up.
- 📟 **Live terminal output** for every transaction, showing the exact command and its output as it streams.
- ⏪ **Downgrade on Arch** from your pacman cache or the Arch Linux Archive.
- 🖲️ **Tray icon** when updates are waiting, polling on an interval you choose.

### 🏗️ The AUR, safely

- 🛡️ **Review before anything builds.** Every PKGBUILD and build file is shown before a build, risky lines are flagged, and you can edit any of them. Exactly the copy you reviewed is what gets built.
- 🔁 **Diffs on updates.** Athena keeps each build you approve, so the next review opens on only what changed since then.
- ⚙️ **Builds** go through `paru` or `yay`, or Athena's own `git clone` + `makepkg` fallback, which builds AUR-only dependencies first. Don't have a helper? **Settings** installs `yay` or `paru` from its official AUR repo.
- 📊 The install preview shows votes, maintainer, and out-of-date/orphan flags.

### 🧰 Housekeeping

| | |
|---|---|
| 🧽 **Free up space** | Package-cache cleanup (`paccache`/`pacman -Sc`, `apt clean`, `dnf clean`) and left-behind dependencies, with sizes shown first. |
| 🗂️ **Groups** | A page for your package manager's groups (pacman groups, dnf groups and environments, tasksel tasks): search them, see what's installed, install a whole group's missing packages, or manage its packages one by one. |
| 🌐 **Repositories** | Switch repos on and off, add new ones (including **COPR** on Fedora), and refresh metadata, for your system and Flatpak remotes. |
| 🪞 **Mirrors** | Rank pacman mirrors with `reflector` and preview the list before it's saved, or turn on DNF's fastest-mirror. |
| 🕘 **History** | One timeline of installs, removals, and upgrades across pacman, APT, DNF, and Flatpak, beside the raw logs. |
| 📦 **Export & Import** | Everything you chose to install, from every source, in one file. On a new machine, pick what's missing and install it. |
| 🔒 **Flatpak** | A page of its own: a **Permissions** editor built on `flatpak override` (a switch per permission), and **App data** and **Leftovers** browsers. |
| 🧩 **Snap management** | Switch a snap's channel, revert to its previous revision, enable or disable it, and connect or disconnect its interfaces. |
| 💾 **Backups** | Flatpak app data and app list to one `.tar.gz`; your dotfiles and config likewise, restored the same way. |
| 🗑️ **User cleanup** | `/tmp`, `~/.cache`, trash, thumbnails, shell history, and recent files, with sizes shown before anything is deleted. |

### ⚙️ And the rest

🔔 New-release notices from GitHub (optional) · 📏 architecture and size columns · 🧹 uninstalls that remove dependencies nothing needs anymore · 📝 optional file logging at a level you choose · 🗂️ a Settings page with General, System, Layout, and Logging tabs

## 📸 Screenshots

<table>
<tr>
<td width="50%" align="center"><b>🛍️ Discover</b><br><sub>Browse every app your sources offer, by category</sub><br><br><img src="docs/screenshots/m3-discover.png" alt="Discover, Graphics category"></td>
<td width="50%" align="center"><b>🔎 One search, every source</b><br><sub>pacman, Flatpak, and AUR results merged into one card</sub><br><br><img src="docs/screenshots/m3-search.png" alt="Searching for Firefox"></td>
</tr>
<tr>
<td align="center"><b>📋 Details sheet</b><br><sub>Screenshots, sources, permissions, and every action</sub><br><br><img src="docs/screenshots/m3-sheet-screenshots.png" alt="Details sheet for Kdenlive"></td>
<td align="center"><b>🛡️ AUR review</b><br><sub>Only what changed since the build you last approved</sub><br><br><img src="docs/screenshots/m3-review-diff.png" alt="PKGBUILD review showing a diff"></td>
</tr>
<tr>
<td align="center"><b>📚 Installed</b><br><sub>Apps from every source, with real names and icons</sub><br><br><img src="docs/screenshots/m3-installed.png" alt="Installed page"></td>
<td align="center"><b>⬆️ Updates</b><br><sub>Every pending update, grouped by source</sub><br><br><img src="docs/screenshots/m3-updates.png" alt="Updates page"></td>
</tr>
<tr>
<td align="center"><b>🌙 Dark theme</b><br><sub>The same scheme, generated for dark</sub><br><br><img src="docs/screenshots/m3-home-dark.png" alt="Home in dark theme"></td>
<td align="center"><b>🌙 Discover in dark</b><br><br><br><img src="docs/screenshots/m3-discover-dark.png" alt="Discover in dark theme"></td>
</tr>
<tr>
<td align="center"><b>👋 First-run wizard</b><br><sub>Pick a look, an accent, and your sources</sub><br><br><img src="docs/screenshots/m3-wizard.png" alt="First-run wizard, look step" width="80%"></td>
<td align="center"><b>⚙️ Settings</b><br><sub>Look, accent color, motion, and title bar</sub><br><br><img src="docs/screenshots/m3-settings.png" alt="Settings page"></td>
</tr>
</table>

<details>
<summary><b>🧰 More: Repositories, History, Mirrors, Export & Import</b></summary>
<br>

<table>
<tr>
<td width="50%" align="center"><b>🌐 Repositories</b><br><br><img src="docs/screenshots/m3-repositories.png" alt="Repositories"></td>
<td width="50%" align="center"><b>🕘 History timeline</b><br><br><img src="docs/screenshots/m3-history.png" alt="History timeline"></td>
</tr>
<tr>
<td align="center"><b>🪞 Mirrors</b><br><br><img src="docs/screenshots/m3-mirrors.png" alt="Mirror ranking"></td>
<td align="center"><b>📦 Export &amp; Import</b><br><br><img src="docs/screenshots/m3-export.png" alt="Export and Import"></td>
</tr>
</table>

</details>

<details>
<summary><b>🕰️ The classic UI (before the Material You redesign)</b></summary>
<br>

These show the pre-redesign interface. Most of these tools still live under **Sources** and **Tools**.

<table>
<tr>
<td width="33%" valign="top" align="center"><b>Switching backends</b><br><br><img src="docs/media/switching-backends.gif" alt="Switching between System and Flatpak"></td>
<td width="33%" valign="top" align="center"><b>Live sandbox permissions</b><br><br><img src="docs/media/toggling-permissions.gif" alt="Toggling a Flatpak permission"></td>
<td width="33%" valign="top" align="center"><b>Search as you go</b><br><br><img src="docs/media/searching.gif" alt="Searching for a package"></td>
</tr>
<tr>
<td align="center"><b>Category browser</b><br><br><img src="docs/screenshots/groups-tab.png" alt="Groups tab"></td>
<td align="center"><b>Advanced search</b><br><br><img src="docs/screenshots/search-tab.png" alt="Search tab"></td>
<td align="center"><b>Repositories</b><br><br><img src="docs/screenshots/repositories-tab.png" alt="Repositories tab"></td>
</tr>
<tr>
<td align="center"><b>Flatpak Permissions</b><br><br><img src="docs/screenshots/flatpak-permissions-tab.png" alt="Flatpak Permissions tab"></td>
<td align="center"><b>Flatpak User Data</b><br><br><img src="docs/screenshots/flatpak-userdata-tab.png" alt="Flatpak User Data tab"></td>
<td align="center"><b>Flatpak Leftover Data</b><br><br><img src="docs/screenshots/flatpak-leftover-tab.png" alt="Flatpak Leftover Data tab"></td>
</tr>
<tr>
<td align="center"><b>Flatpak Backup &amp; Restore</b><br><br><img src="docs/screenshots/flatpak-backup-restore-tab.png" alt="Flatpak Backup and Restore tab"></td>
<td align="center"><b>Installed packages</b><br><br><img src="docs/screenshots/installed-tab.png" alt="Installed tab"></td>
<td align="center"><b>Preferences</b><br><br><img src="docs/screenshots/preferences-general.png" alt="Preferences, General tab"></td>
</tr>
</table>

</details>

## 🐧 Supported systems

| Distro family     | Package manager | Status                             |
| ------------------ | ---------------- | ----------------------------------- |
| Fedora / RHEL       | `dnf5`            | Fully tested                        |
| Debian / Ubuntu     | `apt`             | Implemented, not yet field-tested   |
| Arch Linux          | `pacman`          | Fully tested                        |
| Arch Linux          | AUR (`paru`/`yay`/`makepkg`) | Implemented, not yet field-tested |
| Any of the above    | `flatpak`         | Fully tested                        |
| Any of the above    | `snap`            | Implemented, not yet field-tested   |

## 📥 Installing

Prebuilt packages are published on the [GitHub Releases page](https://github.com/niruxx/Athena/releases) for every tagged version:

| Distro                | Format                                    |
| ---------------------- | ------------------------------------------ |
| Fedora / RHEL           | `.rpm`                                     |
| Debian / Ubuntu         | `.deb`                                     |
| Arch Linux              | [`PKGBUILD`](packaging/arch/PKGBUILD)      |
| Any distro (portable)   | `.AppImage` — no install needed, just run it |

Athena will also let you know inside the app when a new version is available — see **Settings → General → Check for Athena updates on startup**.

## 🔨 Building from source

<details>
<summary><strong>Requirements</strong></summary>

- CMake 3.16+
- A C++17 compiler (GCC or Clang)
- Qt6 development packages: **Widgets**, **Concurrent**, and **Network**

On Fedora:

```bash
sudo dnf install cmake gcc-c++ qt6-qtbase-devel
```

On Debian/Ubuntu:

```bash
sudo apt install cmake g++ qt6-base-dev
```

On Arch:

```bash
sudo pacman -S cmake gcc qt6-base
```

AUR support additionally needs `git` and `base-devel` at runtime (and optionally `paru` or `yay`).

</details>

```bash
git clone https://github.com/niruxx/Athena.git
cd Athena
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The resulting binary is at `build/athena`:

```bash
./build/athena
```

Packaging (`.rpm`/`.deb` via CPack, `.AppImage` via `linuxdeploy`, Arch via `packaging/arch/PKGBUILD`) is what [the CI workflow](.github/workflows/c-cpp.yml) runs on every push and nightly — check there for the exact commands if you want to build a package yourself. Each of Arch, Fedora, and Debian can also be built locally in an isolated container via `packaging/<distro>/build-in-podman.sh` (needs [podman](https://podman.io/)), which is how each format's real package gets built and linked against that distro's own Qt6/glibc without needing a matching host.

## 🔐 A note on privileges

Installing, removing, and reinstalling packages is done through [`pkexec`](https://www.freedesktop.org/software/polkit/docs/latest/pkexec.1.html) (Polkit), which prompts you for your password through your desktop's native authentication dialog — Athena never asks for or stores a password itself. Flatpak install/remove operations use Flatpak's own built-in Polkit integration the same way; Snap has no such integration of its own, so its operations go through `pkexec` too. AUR packages are built as your own user (makepkg refuses to run as root); only the final install and any repository dependencies go through `pkexec`. Athena does that by pointing makepkg's `PACMAN_AUTH`, or the helper's `--sudo` option, at it. Downloading packages (without installing them) is unprivileged on DNF, APT, and Snap; Pacman and Flatpak downloads still go through the same privilege model as a normal install. The Flatpak page and Flatpak backups are the exception in the other direction — they all act on user-level state (`~/.local/share/flatpak/overrides`, `~/.var/app`), so none of them need a privilege prompt at all. The whole Tools page is the same way: every Free up space item for your own files only ever removes files it confirms you own (so a shared `/tmp` is safe to point it at), and its Backup & Restore tab only reads and writes your own home directory — none of it needs a privilege prompt either.

## 📜 Changelog

See [CHANGELOG.md](CHANGELOG.md) for a version-by-version history of what's changed.

## 🧭 Roadmap

What's left from the redesign plan:

- **Ratings and popularity in Discover** — the AppStream catalogs Athena reads have no ratings or download counts, so this needs an online source (such as Flathub's statistics or ODRS reviews); categories are sorted alphabetically until then.
- **New recordings** for the README (the screenshots are current; the GIFs aren't).

## ⚖️ License

Athena is licensed under the [GNU General Public License v3.0](LICENSE).

Icons are [Material Symbols](https://fonts.google.com/icons) by Google, used under the Apache License 2.0 (see [assets/fonts/LICENSE-MaterialSymbols.txt](assets/fonts/LICENSE-MaterialSymbols.txt)); Athena bundles only the icons it uses, fetched by [assets/fonts/update-material-symbols.sh](assets/fonts/update-material-symbols.sh).
