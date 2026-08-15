# distore-qt

A cross-distro GUI package manager built with Qt6 — manage your system's native packages (DNF, APT, or Pacman) and Flatpak apps from one place.

![distore-qt — Installed packages view](docs/screenshots/installed-tab.png)

## Features

- **One app, every major distro** — automatically detects and drives DNF (Fedora/RHEL), APT (Debian/Ubuntu), or Pacman (Arch), plus Flatpak, side by side in the same window.
- **Checkbox multi-select, Synaptic-style** — tick packages across a filtered list and install, reinstall, or uninstall them in a single batch.
- **See what a change actually does before it happens** — install/uninstall confirmations show the real, dependency-resolved plan (what gets pulled in, what becomes unused and gets cleaned up), not just a name you already know.
- **Live terminal output** — every install, uninstall, and reinstall runs in a small terminal window showing the exact command and its output as it streams, so failures are never a mystery.
- **Uninstall really cleans up** — removing a package also removes the dependencies it pulled in that nothing else needs, and a dedicated "Clean Left Behind Dependencies" button sweeps up anything left over from the past.
- **Category browser** — browse packages by group/meta-group (comps groups on Fedora, tasksel tasks on Debian) instead of only searching by name.
- **Repository management** — enable/disable repos, add new ones, and refresh package metadata, for both your native package manager and Flatpak remotes.
- **Update checking** — a dedicated Updates tab per backend, with one-click "Select All" to batch-upgrade everything at once.
- **History** — see recent install/remove/update activity for each backend.
- **Customizable** — theme (light/dark/system), startup tab, compact list rows, and more under Settings.
- **Notifies you about new distore-qt releases** — checks GitHub on startup (optional) and shows a dismissible banner when a newer version is out.

## Supported systems

| Distro family        | Package manager | Status                          |
| --------------------- | ---------------- | -------------------------------- |
| Fedora / RHEL          | `dnf5`            | Fully tested                     |
| Debian / Ubuntu        | `apt`             | Implemented, not yet field-tested |
| Arch Linux             | `pacman`          | Implemented, not yet field-tested |
| Any of the above       | `flatpak`         | Fully tested                     |

## Building from source

### Requirements

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

### Build

```bash
git clone https://github.com/niruxx/distore-qt.git
cd distore-qt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The resulting binary is at `build/distore-qt`:

```bash
./build/distore-qt
```

## Getting the latest release

Prebuilt releases are published on the [GitHub Releases page](https://github.com/niruxx/distore-qt/releases). distore-qt will also let you know inside the app when a new version is available — see **Settings → Check for distore-qt updates on startup**.

## A note on privileges

Installing, removing, and reinstalling packages is done through [`pkexec`](https://www.freedesktop.org/software/polkit/docs/latest/pkexec.1.html) (Polkit), which prompts you for your password through your desktop's native authentication dialog — distore-qt never asks for or stores a password itself. Flatpak operations use Flatpak's own built-in Polkit integration the same way.
