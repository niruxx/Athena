#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ProcessRunner.h"

// Snap management beyond install/remove: channels, revisions, enabling and
// disabling, and interface connections (a snap's permissions), all through
// the `snap` CLI. The parse* functions take that CLI's output so they can
// be checked without snapd installed.
namespace SnapTools {

struct Snap {
    QString name;
    QString version;
    QString revision;
    QString tracking; // "latest/stable", or "-" for a local install
    QString publisher;
    QStringList notes; // "disabled", "classic", "devmode", "base", ...
    bool disabled() const { return notes.contains(QStringLiteral("disabled")); }
};

struct Details {
    QString summary;
    QString publisher;
    QString tracking;
    QString confinement;
    QStringList channels; // channels that have a release, in store order
    bool hasPreviousRevision = false;
};

struct Connection {
    QString interface;
    QString plug; // "firefox:camera"
    QString slot; // ":camera", "gnome-42-2204:gnome-42-2204", or empty when not connected
    bool manual = false; // connected or disconnected by hand rather than automatically
    bool connected() const { return !slot.isEmpty(); }
};

struct Change {
    QDateTime time;
    QString action;    // "installed", "removed", "upgraded", "downgraded"
    QStringList snaps;
};

// Installed snaps (`snap list`), and every revision kept (`snap list --all`).
QVector<Snap> parseList(const QString &output);
QVector<Snap> installed();

// `snap info <name>`: summary, publisher, tracking channel, open channels.
Details parseInfo(const QString &output);
// Also checks `snap list --all <name>` for a kept earlier revision.
Details details(const QString &name);

// `snap connections <name>`: the snap's own plugs (other snaps' plugs into
// its slots are left out).
QVector<Connection> parseConnections(const QString &name, const QString &output);
QVector<Connection> connections(const QString &name);

// `snap changes --abs-time`: finished installs, removals, refreshes, and
// reverts (other changes — connections, configuration — are left out).
QVector<Change> parseChanges(const QString &output);
QVector<Change> recentChanges();

// Privileged commands (snapd needs root for all of these; pkexec).
QVector<ProcessRunner::Command> switchChannelCommands(const QString &name, const QString &channel);
QVector<ProcessRunner::Command> revertCommands(const QString &name);
QVector<ProcessRunner::Command> setEnabledCommands(const QString &name, bool enabled);
QVector<ProcessRunner::Command> connectCommands(const QString &plug, const QString &slot = QString());
QVector<ProcessRunner::Command> disconnectCommands(const QString &plug);

} // namespace SnapTools
