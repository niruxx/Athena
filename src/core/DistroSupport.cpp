#include "DistroSupport.h"

#include <QFile>
#include <QTextStream>

namespace DistroSupport {

namespace {

QString readOsReleaseIds()
{
    QFile file("/etc/os-release");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    QString ids;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.startsWith("ID=") || line.startsWith("ID_LIKE="))
            ids += line.section('=', 1).remove('"').toLower() + ' ';
    }
    return ids;
}

} // namespace

DistroFamily detectDistroFamily()
{
    const QString ids = readOsReleaseIds();

    if (ids.contains("fedora") || ids.contains("rhel") || ids.contains("centos"))
        return DistroFamily::Fedora;
    if (ids.contains("debian") || ids.contains("ubuntu"))
        return DistroFamily::Debian;
    if (ids.contains("arch"))
        return DistroFamily::Arch;
    return DistroFamily::Unknown;
}

bool isFlatpakInstalled()
{
    return ProcessRunner::executableExists("flatpak");
}

bool isSnapInstalled()
{
    return ProcessRunner::executableExists("snap");
}

QVector<ProcessRunner::Command> flatpakInstallCommands()
{
    switch (detectDistroFamily()) {
    case DistroFamily::Fedora:
        return {{"pkexec", {"dnf", "install", "-y", "flatpak"}}};
    case DistroFamily::Debian:
        return {{"pkexec", {"apt-get", "install", "-y", "flatpak"}}};
    case DistroFamily::Arch:
        return {{"pkexec", {"pacman", "-S", "--noconfirm", "flatpak"}}};
    case DistroFamily::Unknown:
        return {};
    }
    return {};
}

QVector<ProcessRunner::Command> snapInstallCommands()
{
    switch (detectDistroFamily()) {
    case DistroFamily::Fedora:
        // Fedora's snapd package doesn't wire up the classic /snap symlink
        // itself; without it, classic-confinement snaps fail to run.
        return {{"pkexec", {"dnf", "install", "-y", "snapd"}},
                {"pkexec", {"ln", "-sf", "/var/lib/snapd/snap", "/snap"}}};
    case DistroFamily::Debian:
        return {{"pkexec", {"apt-get", "install", "-y", "snapd"}}};
    case DistroFamily::Arch:
        // No official-repo package; installing would need an AUR helper
        // (e.g. yay/paru), which this app has no business assuming or
        // driving on the user's behalf.
        return {};
    case DistroFamily::Unknown:
        return {};
    }
    return {};
}

} // namespace DistroSupport
