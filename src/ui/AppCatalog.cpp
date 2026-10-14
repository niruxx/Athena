#include "AppCatalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

namespace AppCatalog {

namespace {

struct Catalog {
    bool loaded = false;
    QVector<Entry> entries;
    QHash<QString, int> byKey; // lowercased lookup key -> index into entries
    QHash<QString, QIcon> icons;
};

Catalog &catalog()
{
    static Catalog instance;
    return instance;
}

QStringList applicationDirs()
{
    QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    dirs << QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/share/applications")
         << QStringLiteral("/var/lib/flatpak/exports/share/applications");
    dirs.removeDuplicates();
    return dirs;
}

// Reads the [Desktop Entry] group only; localized keys (Name[de]=) are
// skipped in favor of the untranslated ones.
bool parseDesktopFile(const QString &path, Entry &entry, QString &execName)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream stream(&file);
    bool inMainGroup = false;
    QString exec;
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.startsWith('[')) {
            inMainGroup = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        if (!inMainGroup || line.startsWith('#'))
            continue;
        const int eq = line.indexOf('=');
        if (eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (key == QLatin1String("Name"))
            entry.name = value;
        else if (key == QLatin1String("Comment"))
            entry.comment = value;
        else if (key == QLatin1String("Icon"))
            entry.iconName = value;
        else if (key == QLatin1String("Exec"))
            exec = value;
        else if ((key == QLatin1String("NoDisplay") || key == QLatin1String("Hidden")) && value == QLatin1String("true"))
            return false;
        else if (key == QLatin1String("Type") && value != QLatin1String("Application"))
            return false;
    }
    entry.filePath = path;
    entry.desktopId = QFileInfo(path).completeBaseName();
    // Exec's program name, minus any path; the flatpak-run wrapper says
    // nothing about which package this is.
    const QStringList execParts = exec.split(' ', Qt::SkipEmptyParts);
    if (!execParts.isEmpty() && !execParts.first().endsWith(QLatin1String("flatpak")))
        execName = QFileInfo(execParts.first()).fileName();
    return !entry.name.isEmpty();
}

void load()
{
    Catalog &c = catalog();
    c.loaded = true;
    for (const QString &dir : applicationDirs()) {
        const QFileInfoList files = QDir(dir).entryInfoList({QStringLiteral("*.desktop")}, QDir::Files);
        for (const QFileInfo &info : files) {
            Entry entry;
            QString execName;
            if (!parseDesktopFile(info.absoluteFilePath(), entry, execName))
                continue;

            const int index = c.entries.size();
            c.entries.append(entry);
            // First file wins for every key (user dirs come first in
            // ApplicationsLocation, matching desktop precedence).
            const QString id = entry.desktopId.toLower();
            c.byKey.insert(id, c.byKey.value(id, index));
            const QString lastComponent = id.section('.', -1);
            if (lastComponent != id)
                c.byKey.insert(lastComponent, c.byKey.value(lastComponent, index));
            if (!execName.isEmpty())
                c.byKey.insert(execName.toLower(), c.byKey.value(execName.toLower(), index));
        }
    }
}

} // namespace

const Entry *find(const QString &packageOrAppId)
{
    Catalog &c = catalog();
    if (!c.loaded)
        load();
    const auto it = c.byKey.constFind(packageOrAppId.toLower());
    return it == c.byKey.constEnd() ? nullptr : &c.entries[it.value()];
}

QIcon iconFor(const Entry &entry)
{
    Catalog &c = catalog();
    const auto cached = c.icons.constFind(entry.iconName);
    if (cached != c.icons.constEnd())
        return cached.value();

    QIcon icon;
    if (QFileInfo(entry.iconName).isAbsolute())
        icon = QIcon(entry.iconName);
    else if (!entry.iconName.isEmpty())
        icon = QIcon::fromTheme(entry.iconName);
    c.icons.insert(entry.iconName, icon);
    return icon;
}

void invalidate()
{
    catalog() = Catalog();
}

bool launch(const Entry &entry)
{
    QFile file(entry.filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    QString exec;
    bool inMainGroup = false;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.startsWith('['))
            inMainGroup = line == QLatin1String("[Desktop Entry]");
        else if (inMainGroup && line.startsWith(QLatin1String("Exec=")))
            exec = line.mid(5);
    }
    // Field codes (%f %U %i ...) stand for files/URLs to open and the icon;
    // launching with none means dropping them. "%%" is a literal percent.
    static const QRegularExpression fieldCode(QStringLiteral("%[fFuUdDnNickvm]"));
    exec.remove(fieldCode).replace(QLatin1String("%%"), QLatin1String("%"));
    QStringList parts = QProcess::splitCommand(exec.simplified());
    if (parts.isEmpty())
        return false;
    const QString program = parts.takeFirst();
    return QProcess::startDetached(program, parts);
}

} // namespace AppCatalog
