#include "SnapTools.h"

#include <QRegularExpression>

namespace SnapTools {

namespace {

QStringList columns(const QString &line)
{
    return line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

// Data lines of a table whose first line is a header starting with
// `firstHeader` (snap prints nothing but an error to stderr when empty).
QStringList tableRows(const QString &output, const QString &firstHeader)
{
    QStringList rows;
    bool header = false;
    for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
        if (!header) {
            header = line.trimmed().startsWith(firstHeader);
            continue;
        }
        rows.append(line);
    }
    return rows;
}

} // namespace

QVector<Snap> parseList(const QString &output)
{
    // Name  Version  Rev  Tracking  Publisher  Notes
    QVector<Snap> snaps;
    for (const QString &line : tableRows(output, QStringLiteral("Name"))) {
        const QStringList fields = columns(line);
        if (fields.size() < 5)
            continue;
        Snap snap;
        snap.name = fields[0];
        snap.version = fields[1];
        snap.revision = fields[2];
        snap.tracking = fields[3];
        snap.publisher = fields[4];
        if (fields.size() > 5 && fields[5] != QLatin1String("-"))
            snap.notes = fields[5].split(',', Qt::SkipEmptyParts);
        snaps.append(snap);
    }
    return snaps;
}

QVector<Snap> installed()
{
    return parseList(ProcessRunner::run("snap", {"list"}, 20000).stdOut);
}

Details parseInfo(const QString &output)
{
    Details details;
    bool inChannels = false;
    for (const QString &line : output.split('\n')) {
        if (inChannels) {
            // "  latest/stable:    131.0 2024-10-15 (5134) 284MB -", or
            // "  esr/candidate:    ↑" for a closed channel that follows the one above.
            if (!line.startsWith(QLatin1Char(' ')))
                inChannels = false;
            else {
                const QString trimmed = line.trimmed();
                const int colon = trimmed.indexOf(QLatin1Char(':'));
                if (colon > 0) {
                    const QString value = trimmed.mid(colon + 1).trimmed();
                    if (!value.isEmpty() && value != QStringLiteral("↑") && value != QStringLiteral("–")
                        && value != QLatin1String("^") && value != QLatin1String("--"))
                        details.channels.append(trimmed.left(colon));
                }
                continue;
            }
        }
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString key = line.left(colon);
        const QString value = line.mid(colon + 1).trimmed();
        if (key == QLatin1String("summary"))
            details.summary = value;
        else if (key == QLatin1String("publisher"))
            details.publisher = value;
        else if (key == QLatin1String("tracking"))
            details.tracking = value;
        else if (key == QLatin1String("confinement"))
            details.confinement = value;
        else if (key == QLatin1String("channels"))
            inChannels = true;
    }
    return details;
}

Details details(const QString &name)
{
    Details result = parseInfo(ProcessRunner::run("snap", {"info", name}, 30000).stdOut);
    // `snap list --all` also shows the revisions kept for reverting
    // (marked "disabled", same name).
    const QVector<Snap> revisions = parseList(ProcessRunner::run("snap", {"list", "--all", name}, 20000).stdOut);
    int kept = 0;
    for (const Snap &snap : revisions) {
        if (snap.name == name)
            ++kept;
    }
    result.hasPreviousRevision = kept > 1;
    return result;
}

QVector<Connection> parseConnections(const QString &name, const QString &output)
{
    // Interface  Plug  Slot  Notes
    QVector<Connection> list;
    const QString ownPrefix = name + QLatin1Char(':');
    for (const QString &line : tableRows(output, QStringLiteral("Interface"))) {
        const QStringList fields = columns(line);
        if (fields.size() < 3 || !fields[1].startsWith(ownPrefix))
            continue;
        Connection connection;
        connection.interface = fields[0];
        connection.plug = fields[1];
        connection.slot = fields[2] == QLatin1String("-") ? QString() : fields[2];
        connection.manual = fields.size() > 3 && fields[3].contains(QLatin1String("manual"));
        list.append(connection);
    }
    return list;
}

QVector<Connection> connections(const QString &name)
{
    // Given a snap, this lists its plugs whether connected or not.
    return parseConnections(name, ProcessRunner::run("snap", {"connections", name}, 20000).stdOut);
}

QVector<Change> parseChanges(const QString &output)
{
    // ID  Status  Spawn  Ready  Summary — with --abs-time, Spawn and Ready
    // are RFC 3339 timestamps ("-" while not ready).
    static const QRegularExpression row(QStringLiteral(R"(^\s*(\d+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(.+?)\s*$)"));
    static const QRegularExpression quoted(QStringLiteral("\"([^\"]+)\""));
    static const QRegularExpression fromChannel(QStringLiteral(R"re(\s+from\s+"[^"]*"\s+channel.*$)re"));
    QVector<Change> changes;
    for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch m = row.match(line);
        if (!m.hasMatch() || m.captured(2) != QLatin1String("Done"))
            continue;
        QString summary = m.captured(5);
        summary.remove(fromChannel);
        const QString verb = summary.section(QLatin1Char(' '), 0, 0);
        QString action;
        if (verb == QLatin1String("Install"))
            action = QStringLiteral("installed");
        else if (verb == QLatin1String("Remove"))
            action = QStringLiteral("removed");
        else if (verb == QLatin1String("Refresh") || verb == QLatin1String("Auto-refresh"))
            action = QStringLiteral("upgraded");
        else if (verb == QLatin1String("Revert"))
            action = QStringLiteral("downgraded");
        else
            continue;
        Change change;
        change.action = action;
        const QString ready = m.captured(4) != QLatin1String("-") ? m.captured(4) : m.captured(3);
        change.time = QDateTime::fromString(ready, Qt::ISODate);
        for (auto it = quoted.globalMatch(summary); it.hasNext();)
            change.snaps.append(it.next().captured(1));
        if (!change.snaps.isEmpty())
            changes.append(change);
    }
    return changes;
}

QVector<Change> recentChanges()
{
    return parseChanges(ProcessRunner::run("snap", {"changes", "--abs-time"}, 20000).stdOut);
}

QVector<ProcessRunner::Command> switchChannelCommands(const QString &name, const QString &channel)
{
    return {{"pkexec", {"snap", "refresh", QStringLiteral("--channel=%1").arg(channel), name}}};
}

QVector<ProcessRunner::Command> revertCommands(const QString &name)
{
    return {{"pkexec", {"snap", "revert", name}}};
}

QVector<ProcessRunner::Command> setEnabledCommands(const QString &name, bool enabled)
{
    return {{"pkexec", {"snap", enabled ? QStringLiteral("enable") : QStringLiteral("disable"), name}}};
}

QVector<ProcessRunner::Command> connectCommands(const QString &plug, const QString &slot)
{
    QStringList args = {"snap", "connect", plug};
    if (!slot.isEmpty())
        args.append(slot);
    return {{"pkexec", args}};
}

QVector<ProcessRunner::Command> disconnectCommands(const QString &plug)
{
    return {{"pkexec", {"snap", "disconnect", plug}}};
}

} // namespace SnapTools
