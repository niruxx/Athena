<div align="center">

# Athena

**A cross-distro GUI package manager built with Qt6.**

Manage your system's native packages (DNF, APT, or Pacman), plus Flatpak and Snap apps, all from one window.

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Build packages](https://github.com/niruxx/Athena/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/niruxx/Athena/actions/workflows/c-cpp.yml)
![Qt6](https://img.shields.io/badge/Qt-6-41cd52.svg?logo=qt)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg?logo=cplusplus)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)

![Athena — Installed packages view](docs/screenshots/installed-tab.png)

</div>

## Contents

- [Features](#features)
- [Supported systems](#supported-systems)
- [Installing](#installing)
- [Building from source](#building-from-source)
- [A note on privileges](#a-note-on-privileges)
- [Changelog](#changelog)
- [License](#license)

## Features

- 🖥️ **One app, every major distro** — automatically detects and drives DNF (Fedora/RHEL), APT (Debian/Ubuntu), or Pacman (Arch), plus Flatpak and Snap, side by side in the same window.
- ☑️ **Checkbox multi-select, Synaptic-style** — tick packages across a filtered list and install, reinstall, or uninstall them in a single batch.
- 🔍 **See what a change actually does before it happens** — install/uninstall confirmations show the real, dependency-resolved plan (what gets pulled in, what becomes unused and gets cleaned up), not just a name you already know.
- 📟 **Live terminal output** — every install, uninstall, and reinstall runs in a small terminal window showing the exact command and its output as it streams, so failures are never a mystery.
- 🧹 **Uninstall really cleans up** — removing a package also removes the dependencies it pulled in that nothing else needs, and a dedicated "Clean Left Behind Dependencies" button sweeps up anything left over from the past.
- 📂 **Category browser** — browse packages by group/meta-group (comps groups on Fedora, tasksel tasks on Debian, pacman groups on Arch) instead of only searching by name.
- 🌐 **Repository management** — enable/disable repos, add new ones, and refresh package metadata, for your native package manager and Flatpak remotes alike.
- ⬆️ **Update checking** — a dedicated Updates tab per backend, with one-click "Select All" to batch-upgrade everything at once.
- 🕘 **History** — see recent install/remove/update activity for each backend.
- 👋 **First-run onboarding** — detects your distro and offers to install Flatpak/Snap support on the spot if it's missing.
- ⚙️ **Customizable** — theme (light/dark/system), startup tab, compact list rows, and a full Preferences dialog organized into General, System, Layout, and Logging Options tabs.
- 🔔 **Notifies you about new Athena releases** — checks GitHub on startup (optional) and shows a dismissible banner when a newer version is out.
- 🖲️ **System tray integration** — a tray icon appears when updates are available, with a right-click menu for Settings, Update (installs everything pending in one go), and Quit. Polls on a configurable interval.
- 🔍 **Advanced search** — beyond a plain keyword search: a **Dependency Query** mode finds packages that provide or require a given capability (e.g. a library soname), plus **Repository** and **Architecture** filters to narrow results.
- ⬇️ **Download without installing** — grab a copy of a package (optionally plus its not-yet-installed dependencies) to a folder of your choice, instead of installing it.
- 📏 **Architecture and size columns** — optional columns in every package list (toggle from the table header), and a package's homepage link — when the backend reports one — shown right in its details panel.
- 📝 **Optional file logging** — enable logging to a folder of your choice with a configurable verbosity level, for troubleshooting.

## Supported systems

| Distro family     | Package manager | Status                             |
| ------------------ | ---------------- | ----------------------------------- |
| Fedora / RHEL       | `dnf5`            | Fully tested                        |
| Debian / Ubuntu     | `apt`             | Implemented, not yet field-tested   |
| Arch Linux          | `pacman`          | Implemented, not yet field-tested   |
| Any of the above    | `flatpak`         | Fully tested                        |
| Any of the above    | `snap`            | Implemented, not yet field-tested   |

## Installing

Prebuilt packages are published on the [GitHub Releases page](https://github.com/niruxx/Athena/releases) for every tagged version:

| Distro                | Format                                    |
| ---------------------- | ------------------------------------------ |
| Fedora / RHEL           | `.rpm`                                     |
| Debian / Ubuntu         | `.deb`                                     |
| Arch Linux              | [`PKGBUILD`](packaging/arch/PKGBUILD)      |
| Any distro (portable)   | `.AppImage` — no install needed, just run it |

Athena will also let you know inside the app when a new version is available — see **Preferences → Check for Athena updates on startup**.

## Building from source

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

Packaging (`.rpm`/`.deb` via CPack, `.AppImage` via `linuxdeploy`, Arch via `packaging/arch/PKGBUILD`) is what [the CI workflow](.github/workflows/c-cpp.yml) runs on every push — check there for the exact commands if you want to build a package yourself.

## A note on privileges

Installing, removing, and reinstalling packages is done through [`pkexec`](https://www.freedesktop.org/software/polkit/docs/latest/pkexec.1.html) (Polkit), which prompts you for your password through your desktop's native authentication dialog — Athena never asks for or stores a password itself. Flatpak operations use Flatpak's own built-in Polkit integration the same way; Snap has no such integration of its own, so its operations go through `pkexec` too. Downloading packages (without installing them) is unprivileged on DNF, APT, and Snap; Pacman and Flatpak downloads still go through the same privilege model as a normal install.

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for a version-by-version history of what's changed.

## License

Athena is licensed under the [GNU General Public License v3.0](LICENSE).
