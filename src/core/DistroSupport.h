#pragma once

#include <QVector>

#include "ProcessRunner.h"

// Distro family detection (shared by BackendFactory, which picks the
// native backend from it) plus the bootstrap commands for installing
// Flatpak/Snap support via each distro's own package manager — used by
// SettingsPage and FirstRunDialog to offer installing them from within
// the app instead of requiring the user to already have them.
namespace DistroSupport {

enum class DistroFamily { Fedora, Debian, Arch, Unknown };

// Reads /etc/os-release (ID / ID_LIKE) to classify the running system.
DistroFamily detectDistroFamily();

bool isFlatpakInstalled();
bool isSnapInstalled();

// The command(s) that install Flatpak/Snap support via this distro's own
// package manager, wrapped in pkexec. Empty if this distro has no known
// official path — notably Snap on Arch, which has no official-repo
// package and would need an AUR helper this app doesn't drive.
QVector<ProcessRunner::Command> flatpakInstallCommands();
QVector<ProcessRunner::Command> snapInstallCommands();

// The reverse: uninstalls Flatpak/Snap support via the same package
// manager. Unlike the install side, removal works even on distros with no
// official install path for that package (e.g. Snap on Arch), since an
// already-installed package is already known to the system's package
// database (pacman, in that case) regardless of how it got there.
QVector<ProcessRunner::Command> flatpakRemoveCommands();
QVector<ProcessRunner::Command> snapRemoveCommands();

} // namespace DistroSupport
