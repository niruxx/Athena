#pragma once

#include <QIcon>
#include <QString>

// Installed desktop applications, read from .desktop files (system,
// user, and Flatpak exports), so package rows can show a real app name,
// summary, and icon, and so "is this package a desktop app?" has an answer.
//
// Matching a package to its .desktop file is a heuristic: by file name
// ("firefox.desktop"), by the last component of a reverse-DNS name
// ("org.kde.kdenlive.desktop" for kdenlive), or by its Exec program. Flatpak
// app IDs match their .desktop file name exactly. A proper AppStream-based
// catalog (which also covers apps that aren't installed) is on the TODO list.
namespace AppCatalog {

struct Entry {
    QString desktopId; // file name without .desktop
    QString name;
    QString comment;
    QString iconName; // theme icon name or absolute path
    QString filePath;
};

// The entry for a package name or Flatpak app ID, or nullptr. Loads the
// catalog on first use (a few hundred small files; UI thread is fine).
const Entry *find(const QString &packageOrAppId);

// The entry's icon, or a null QIcon if the theme doesn't have it.
QIcon iconFor(const Entry &entry);

// Drops the cache so the next find() re-reads .desktop files (after an
// install or removal).
void invalidate();

// Starts the app a .desktop file describes (its Exec line, minus the
// file/URL placeholders), without needing gio or a desktop's launcher.
bool launch(const Entry &entry);

} // namespace AppCatalog
