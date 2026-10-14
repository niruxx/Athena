#include "SystemTools.h"

#include <algorithm>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QXmlStreamReader>

namespace SystemTools {

namespace {

using ProcessRunner::Result;

QString fetch(const QString &url, int timeoutMs = 30000)
{
    const Result result = ProcessRunner::run(
        "curl", {"--fail", "--silent", "--show-error", "--location", "--connect-timeout", "10", "--max-time",
                 QString::number(timeoutMs / 1000), "--", url},
        timeoutMs + 5000);
    return result.started && result.exitCode == 0 ? result.stdOut : QString();
}

QString dnfProgram()
{
    return ProcessRunner::executableExists("dnf5") ? QStringLiteral("dnf5") : QStringLiteral("dnf");
}

// pacman.log timestamps look like [2026-10-08T22:46:14+0000]; Qt's ISO
// parser wants a colon in the offset.
QDateTime parsePacmanTimestamp(QString stamp)
{
    static const QRegularExpression offset(QStringLiteral(R"(([+-]\d\d)(\d\d)$)"));
    stamp.replace(offset, QStringLiteral("\\1:\\2"));
    return QDateTime::fromString(stamp, Qt::ISODate);
}

QStringList readLines(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QStringList lines;
    QTextStream stream(&file);
    while (!stream.atEnd())
        lines << stream.readLine();
    return lines;
}

// pacman's version comparison (rpmvercmp), enough to order a package's own
// versions: epoch, then alternating numeric/alphabetic segments.
int compareVersions(const QString &a, const QString &b)
{
    auto split = [](const QString &v, QString &epoch, QString &rest) {
        const int colon = v.indexOf(':');
        epoch = colon > 0 ? v.left(colon) : QStringLiteral("0");
        rest = colon > 0 ? v.mid(colon + 1) : v;
    };
    QString ea, ra, eb, rb;
    split(a, ea, ra);
    split(b, eb, rb);
    if (ea.toLongLong() != eb.toLongLong())
        return ea.toLongLong() < eb.toLongLong() ? -1 : 1;
    static const QRegularExpression segment(QStringLiteral("(\\d+|[A-Za-z]+)"));
    auto sa = segment.globalMatch(ra);
    auto sb = segment.globalMatch(rb);
    while (sa.hasNext() && sb.hasNext()) {
        const QString x = sa.next().captured(1);
        const QString y = sb.next().captured(1);
        const bool xd = x[0].isDigit();
        const bool yd = y[0].isDigit();
        if (xd != yd)
            return xd ? 1 : -1; // numeric sorts newer than alphabetic
        if (xd) {
            // Compare digit runs as numbers of any length: strip leading
            // zeros, then the longer run is larger, else compare digits.
            auto strip = [](QString s) {
                while (s.size() > 1 && s.startsWith('0'))
                    s.remove(0, 1);
                return s;
            };
            const QString xs = strip(x);
            const QString ys = strip(y);
            if (xs.size() != ys.size())
                return xs.size() < ys.size() ? -1 : 1;
            if (xs != ys)
                return xs < ys ? -1 : 1;
        } else if (x != y) {
            return x < y ? -1 : 1;
        }
    }
    if (sa.hasNext())
        return 1;
    if (sb.hasNext())
        return -1;
    return 0;
}

} // namespace

// ---- Snapshots ----------------------------------------------------------------

SnapshotTool snapshotTool()
{
    if (ProcessRunner::executableExists("snapper")) {
        const Result configs = ProcessRunner::run("snapper", {"list-configs"}, 5000);
        if (configs.started && configs.exitCode == 0 && configs.stdOut.contains(QLatin1String("root")))
            return SnapshotTool::Snapper;
    }
    if (ProcessRunner::executableExists("timeshift"))
        return SnapshotTool::Timeshift;
    return SnapshotTool::None;
}

QString snapshotToolName(SnapshotTool tool)
{
    switch (tool) {
    case SnapshotTool::Snapper:
        return QStringLiteral("Snapper");
    case SnapshotTool::Timeshift:
        return QStringLiteral("Timeshift");
    case SnapshotTool::None:
        break;
    }
    return QString();
}

QVector<ProcessRunner::Command> snapshotCommands(const QString &description)
{
    switch (snapshotTool()) {
    case SnapshotTool::Snapper:
        return {{"pkexec", {"snapper", "-c", "root", "create", "--cleanup-algorithm", "number", "--description", description}}};
    case SnapshotTool::Timeshift:
        return {{"pkexec", {"timeshift", "--create", "--scripted", "--comments", description}}};
    case SnapshotTool::None:
        break;
    }
    return {};
}

QVector<Snapshot> listSnapshots(int limit)
{
    QVector<Snapshot> snapshots;
    if (snapshotTool() != SnapshotTool::Snapper)
        return snapshots;
    const Result result = ProcessRunner::run("snapper", {"--jsonout", "-c", "root", "list"}, 10000);
    const QJsonArray entries = QJsonDocument::fromJson(result.stdOut.toUtf8()).object().value("root").toArray();
    for (const QJsonValue &entry : entries) {
        const QJsonObject o = entry.toObject();
        if (o.value("number").toInt() == 0)
            continue; // "current"
        snapshots.append({QString::number(o.value("number").toInt()), QDateTime::fromString(o.value("date").toString(), Qt::ISODate),
                          o.value("description").toString()});
    }
    std::reverse(snapshots.begin(), snapshots.end());
    if (snapshots.size() > limit)
        snapshots.resize(limit);
    return snapshots;
}

// ---- Package cache ----------------------------------------------------------------

QString packageCacheDir(const QString &nativeKey)
{
    if (nativeKey == QLatin1String("pacman"))
        return QStringLiteral("/var/cache/pacman/pkg");
    if (nativeKey == QLatin1String("apt"))
        return QStringLiteral("/var/cache/apt/archives");
    if (QFileInfo::exists(QStringLiteral("/var/cache/libdnf5")))
        return QStringLiteral("/var/cache/libdnf5");
    return QStringLiteral("/var/cache/dnf");
}

qint64 directorySize(const QString &path)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

QVector<CacheCleanup> cacheCleanupOptions(const QString &nativeKey, int keepVersions)
{
    if (nativeKey == QLatin1String("pacman")) {
        if (ProcessRunner::executableExists("paccache")) {
            return {
                {QObject::tr("Old versions of installed packages"),
                 QObject::tr("Keeps the newest %1 of each, for downgrading (paccache -rk%1)").arg(keepVersions),
                 {{"pkexec", {"paccache", "-r", QStringLiteral("-k%1").arg(keepVersions)}}}},
                {QObject::tr("Cached packages that are no longer installed"), QObject::tr("paccache -ruk0"),
                 {{"pkexec", {"paccache", "-r", "-u", "-k0"}}}},
            };
        }
        return {{QObject::tr("Cached packages that are no longer installed"),
                 QObject::tr("pacman -Sc. Install pacman-contrib to also trim old versions of installed packages."),
                 {{"pkexec", {"pacman", "-Sc", "--noconfirm"}}}}};
    }
    if (nativeKey == QLatin1String("apt")) {
        return {
            {QObject::tr("Downloaded packages no longer available"), QObject::tr("apt-get autoclean"),
             {{"pkexec", {"apt-get", "autoclean", "-y"}}}},
            {QObject::tr("All downloaded packages"), QObject::tr("apt-get clean"), {{"pkexec", {"apt-get", "clean"}}}},
        };
    }
    return {{QObject::tr("Downloaded packages"), QObject::tr("%1 clean packages").arg(dnfProgram()),
             {{"pkexec", {dnfProgram(), "clean", "packages"}}}}};
}

// ---- Mirrors ------------------------------------------------------------------------

QString mirrorListPath(const QString &nativeKey)
{
    if (nativeKey == QLatin1String("pacman"))
        return QStringLiteral("/etc/pacman.d/mirrorlist");
    if (nativeKey == QLatin1String("dnf"))
        return QStringLiteral("/etc/dnf/dnf.conf");
    return QString();
}

bool mirrorRankingAvailable(const QString &nativeKey)
{
    return nativeKey == QLatin1String("pacman") || nativeKey == QLatin1String("dnf");
}

QString mirrorRankingRequirement(const QString &nativeKey)
{
    if (nativeKey == QLatin1String("pacman") && !ProcessRunner::executableExists("reflector")
        && !ProcessRunner::executableExists("rate-mirrors"))
        return QStringLiteral("reflector");
    return QString();
}

QVector<ProcessRunner::Command> rankMirrorsCommands(const QString &nativeKey, const QString &outputFile)
{
    if (nativeKey != QLatin1String("pacman"))
        return {};
    if (ProcessRunner::executableExists("rate-mirrors"))
        return {{"rate-mirrors", {"--save", outputFile, "arch"}}};
    return {{"reflector", {"--protocol", "https", "--latest", "40", "--sort", "rate", "--number", "20", "--save", outputFile}}};
}

QVector<ProcessRunner::Command> applyMirrorsCommands(const QString &nativeKey, const QString &rankedFile)
{
    if (nativeKey == QLatin1String("pacman")) {
        // One privileged step (one password prompt): back up, then install.
        return {{"pkexec", {"sh", "-c", "cp -- \"$1\" \"$1.athena-backup\" && install -m 644 -- \"$2\" \"$1\"", "athena",
                            mirrorListPath(nativeKey), rankedFile}}};
    }
    if (nativeKey == QLatin1String("dnf")) {
        // DNF ranks mirrors itself once fastestmirror is on.
        return {{"pkexec", {"sh", "-c",
                            "f=\"$1\"; cp -- \"$f\" \"$f.athena-backup\"; "
                            "if grep -q '^fastestmirror' \"$f\"; then sed -i 's/^fastestmirror.*/fastestmirror=True/' \"$f\"; "
                            "else printf 'fastestmirror=True\\n' >> \"$f\"; fi",
                            "athena", mirrorListPath(nativeKey)}}};
    }
    return {};
}

// ---- Downgrade ------------------------------------------------------------------------

QVector<PackageVersion> downgradeCandidates(const QString &package, const QString &installedVersion)
{
    // name-version-release-arch.pkg.tar.(zst|xz); versions start with a
    // digit (after an optional epoch), which keeps "linux" from matching
    // "linux-firmware-…".
    const QRegularExpression pattern(QStringLiteral(R"(^%1-((?:\d+:)?\d[^-]*-[^-]+)-(?:x86_64|any|aarch64|i686)\.pkg\.tar\.(?:zst|xz)$)")
                                         .arg(QRegularExpression::escape(package)));
    QVector<PackageVersion> versions;
    QSet<QString> seen;

    const QString cache = packageCacheDir(QStringLiteral("pacman"));
    for (const QString &file : QDir(cache).entryList({package + QStringLiteral("-*.pkg.tar.*")}, QDir::Files)) {
        const QRegularExpressionMatch match = pattern.match(file);
        if (!match.hasMatch() || match.captured(1) == installedVersion || seen.contains(match.captured(1)))
            continue;
        seen.insert(match.captured(1));
        versions.append({match.captured(1), cache + '/' + file, true});
    }

    const QString base = QStringLiteral("https://archive.archlinux.org/packages/%1/%2/").arg(package.left(1), package);
    static const QRegularExpression href(QStringLiteral(R"re(href="([^"]+\.pkg\.tar\.(?:zst|xz))")re"));
    auto links = href.globalMatch(fetch(base, 20000));
    while (links.hasNext()) {
        const QString file = links.next().captured(1);
        const QRegularExpressionMatch match = pattern.match(file);
        if (!match.hasMatch() || match.captured(1) == installedVersion || seen.contains(match.captured(1)))
            continue;
        seen.insert(match.captured(1));
        versions.append({match.captured(1), base + file, false});
    }

    std::sort(versions.begin(), versions.end(),
              [](const PackageVersion &a, const PackageVersion &b) { return compareVersions(a.version, b.version) > 0; });
    if (versions.size() > 40)
        versions.resize(40);
    return versions;
}

QVector<ProcessRunner::Command> installVersionCommands(const PackageVersion &version)
{
    return {{"pkexec", {"pacman", "-U", "--noconfirm", "--", version.location}}};
}

// ---- Arch news ----------------------------------------------------------------------------

QVector<NewsItem> archNews(int limit)
{
    QVector<NewsItem> items;
    QXmlStreamReader xml(fetch(QStringLiteral("https://archlinux.org/feeds/news/")));
    NewsItem current;
    bool inItem = false;
    while (!xml.atEnd() && items.size() < limit) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto name = xml.name();
            if (name == QLatin1String("item")) {
                inItem = true;
                current = NewsItem();
            } else if (inItem && name == QLatin1String("title")) {
                current.title = xml.readElementText().trimmed();
            } else if (inItem && name == QLatin1String("link")) {
                current.link = xml.readElementText().trimmed();
            } else if (inItem && name == QLatin1String("pubDate")) {
                current.published = QDateTime::fromString(xml.readElementText().trimmed(), Qt::RFC2822Date);
            } else if (inItem && name == QLatin1String("description")) {
                current.html = xml.readElementText();
            }
        } else if (xml.isEndElement() && xml.name() == QLatin1String("item")) {
            inItem = false;
            current.manualIntervention = current.title.contains(QLatin1String("intervention"), Qt::CaseInsensitive);
            items.append(current);
        }
    }
    return items;
}

QDateTime lastSystemUpgrade()
{
    const QStringList lines = readLines(QStringLiteral("/var/log/pacman.log"));
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        if (it->contains(QLatin1String("starting full system upgrade")))
            return parsePacmanTimestamp(it->section(']', 0, 0).mid(1));
    }
    return {};
}

// ---- Activity timeline --------------------------------------------------------------------------

QVector<Activity> recentActivity(const QString &nativeKey, bool includeFlatpak, int limit)
{
    QVector<Activity> activity;

    if (nativeKey == QLatin1String("pacman")) {
        static const QRegularExpression line(
            QStringLiteral(R"(^\[([^\]]+)\] \[ALPM\] (installed|upgraded|removed|reinstalled|downgraded) (\S+) \((.*)\)$)"));
        const QStringList lines = readLines(QStringLiteral("/var/log/pacman.log"));
        for (auto it = lines.crbegin(); it != lines.crend() && activity.size() < limit * 4; ++it) {
            const QRegularExpressionMatch m = line.match(*it);
            if (m.hasMatch())
                activity.append({parsePacmanTimestamp(m.captured(1)), m.captured(2), m.captured(3),
                                 QString(m.captured(4)).replace(QLatin1String(" -> "), QStringLiteral(" → ")), nativeKey});
        }
    } else if (nativeKey == QLatin1String("apt")) {
        // /var/log/apt/history.log: blocks of Start-Date / Install: / Upgrade: / Remove:.
        const QStringList lines = readLines(QStringLiteral("/var/log/apt/history.log"));
        QDateTime when;
        static const QRegularExpression entry(QStringLiteral(R"(([^\s,:]+)(?::\S+)? \(([^)]*)\))"));
        for (const QString &raw : lines) {
            if (raw.startsWith(QLatin1String("Start-Date:"))) {
                when = QDateTime::fromString(raw.mid(11).simplified(), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                continue;
            }
            const QString kind = raw.section(':', 0, 0);
            const QString action = kind == QLatin1String("Install") ? QStringLiteral("installed")
                : kind == QLatin1String("Upgrade")                  ? QStringLiteral("upgraded")
                : (kind == QLatin1String("Remove") || kind == QLatin1String("Purge")) ? QStringLiteral("removed")
                                                                                      : QString();
            if (action.isEmpty())
                continue;
            auto it = entry.globalMatch(raw.section(':', 1));
            while (it.hasNext()) {
                const QRegularExpressionMatch m = it.next();
                activity.append({when, action, m.captured(1), QString(m.captured(2)).replace(QLatin1String(", "), QStringLiteral(" → ")), nativeKey});
            }
        }
        std::reverse(activity.begin(), activity.end());
    } else if (nativeKey == QLatin1String("dnf")) {
        // dnf's history is per transaction rather than per package: one
        // entry per transaction, named after its command line.
        const Result result = ProcessRunner::run(dnfProgram(), {"history", "list"}, 20000);
        static const QRegularExpression row(QStringLiteral(R"(^\s*(\d+)\s+(.*?)\s{2,}(\d{4}-\d\d-\d\d \d\d:\d\d(?::\d\d)?)\s+(\S.*?)\s{2,}(\d+))"));
        for (const QString &raw : result.stdOut.split('\n')) {
            const QRegularExpressionMatch m = row.match(raw);
            if (!m.hasMatch())
                continue;
            QDateTime when = QDateTime::fromString(m.captured(3), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            if (!when.isValid())
                when = QDateTime::fromString(m.captured(3), QStringLiteral("yyyy-MM-dd HH:mm"));
            const QString action = m.captured(4).toLower();
            activity.append({when,
                             action.contains(QLatin1String("remov")) ? QStringLiteral("removed")
                                 : action.contains(QLatin1String("upgrad")) ? QStringLiteral("upgraded")
                                                                            : QStringLiteral("installed"),
                             m.captured(2).trimmed(), QObject::tr("%n package(s)", "", m.captured(5).toInt()), nativeKey});
        }
    }

    if (includeFlatpak && ProcessRunner::executableExists("flatpak")) {
        const Result result = ProcessRunner::run("flatpak", {"history", "--columns=time,change,application,branch"}, 20000);
        const QDate today = QDate::currentDate();
        for (const QString &raw : result.stdOut.split('\n', Qt::SkipEmptyParts)) {
            const QStringList fields = raw.split('\t');
            if (fields.size() < 3 || fields[2].trimmed().isEmpty())
                continue;
            // "Oct  8 17:48:28" has no year: this year, unless that's in the future.
            QDateTime when = QDateTime::fromString(QStringLiteral("%1 %2").arg(today.year()).arg(fields[0].simplified()),
                                                   QStringLiteral("yyyy MMM d HH:mm:ss"));
            if (when.isValid() && when > QDateTime::currentDateTime())
                when = when.addYears(-1);
            const QString change = fields[1];
            const QString action = change.contains(QLatin1String("uninstall")) ? QStringLiteral("removed")
                : change.contains(QLatin1String("update"))                      ? QStringLiteral("upgraded")
                                                                                : QStringLiteral("installed");
            activity.append({when, action, fields[2].trimmed(), fields.value(3).trimmed(), QStringLiteral("flatpak")});
        }
    }

    std::stable_sort(activity.begin(), activity.end(), [](const Activity &a, const Activity &b) { return a.time > b.time; });
    if (activity.size() > limit)
        activity.resize(limit);
    return activity;
}

// ---- Export / import ----------------------------------------------------------------------------

QStringList explicitlyInstalled(const QString &nativeKey)
{
    Result result;
    if (nativeKey == QLatin1String("pacman"))
        result = ProcessRunner::run("pacman", {"-Qqen"}, 30000); // native (non-AUR) explicit installs
    else if (nativeKey == QLatin1String("apt"))
        result = ProcessRunner::run("apt-mark", {"showmanual"}, 30000);
    else
        result = ProcessRunner::run(dnfProgram(), {"repoquery", "--userinstalled", "--qf", "%{name}\n"}, 60000);
    QStringList names = result.stdOut.split('\n', Qt::SkipEmptyParts);
    for (QString &name : names)
        name = name.trimmed();
    names.removeDuplicates();
    names.sort();
    return names;
}

} // namespace SystemTools
