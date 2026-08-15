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

} // namespace DistroSupport
