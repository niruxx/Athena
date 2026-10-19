#include "AppStreamIndex.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QProcess>
#include <QXmlStreamReader>
#include <QtConcurrent/QtConcurrent>


namespace {

QByteArray readCatalog(const QString &path)
{
    if (path.endsWith(QLatin1String(".gz"))) {
        // gzip is part of every distro's base system; Qt's own qUncompress
        // only handles zlib streams, not gzip files.
        // Raw bytes rather than ProcessRunner's QString output: Flathub's
        // catalog is ~50 MB, not worth a round trip through UTF-16.
        QProcess gzip;
        gzip.start(QStringLiteral("gzip"), {QStringLiteral("-dc"), QStringLiteral("--"), path});
        if (!gzip.waitForStarted(5000))
            return {};
        QByteArray data;
        while (gzip.state() != QProcess::NotRunning) {
            gzip.waitForReadyRead(60000);
            data += gzip.readAllStandardOutput();
        }
        data += gzip.readAllStandardOutput();
        return gzip.exitStatus() == QProcess::NormalExit && gzip.exitCode() == 0 ? data : QByteArray();
    }
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// The cached icon closest to 64px (catalogs list several sizes).
QString pickIcon(const QVector<QPair<int, QString>> &cached, const QString &iconRoot)
{
    if (cached.isEmpty() || iconRoot.isEmpty())
        return {};
    const auto best = std::min_element(cached.begin(), cached.end(), [](const auto &a, const auto &b) {
        return std::abs(a.first - 64) < std::abs(b.first - 64);
    });
    const QString path = QStringLiteral("%1/%2x%2/%3").arg(iconRoot).arg(best->first).arg(best->second);
    return QFileInfo::exists(path) ? path : QString();
}

// Text of a <description>: paragraphs and list items, flattened.
QString readDescription(QXmlStreamReader &xml)
{
    QStringList blocks;
    while (!(xml.isEndElement() && xml.name() == QLatin1String("description")) && !xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && (xml.name() == QLatin1String("p") || xml.name() == QLatin1String("li"))) {
            const bool item = xml.name() == QLatin1String("li");
            const QString text = xml.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
            if (!text.isEmpty())
                blocks << (item ? QStringLiteral("• ") + text : text);
        }
    }
    return blocks.join(QStringLiteral("\n\n"));
}

bool untranslated(const QXmlStreamReader &xml)
{
    return !xml.attributes().hasAttribute(QLatin1String("xml:lang"));
}

// iconBase: for distro catalogs, the folder holding one icon folder per
// catalog origin (…/icons/<origin>/64x64/…); for a Flatpak remote, the
// remote's own icon folder (…/active/icons/64x64/…).
void parseXmlCatalog(const QByteArray &data, AppStreamApp::Kind kind, const QString &iconBase, const QString &remote,
                     QVector<AppStreamApp> &out)
{
    QXmlStreamReader xml(data);
    QString origin;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;
        if (xml.name() == QLatin1String("components")) {
            origin = xml.attributes().value(QLatin1String("origin")).toString();
            continue;
        }
        if (xml.name() != QLatin1String("component"))
            continue;
        const QString type = xml.attributes().value(QLatin1String("type")).toString();
        if (type != QLatin1String("desktop-application") && type != QLatin1String("desktop")) {
            xml.skipCurrentElement();
            continue;
        }

        AppStreamApp app;
        app.kind = kind;
        app.origin = remote.isEmpty() ? origin : remote;
        QVector<QPair<int, QString>> cachedIcons;
        QString bundleRef;
        while (!(xml.isEndElement() && xml.name() == QLatin1String("component")) && !xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement())
                continue;
            const auto tag = xml.name();
            // Subtrees that reuse component-level tag names: <developer>
            // has its own <name>, each <release> its own <description>
            // (release notes), and <provides>/<requires>/... their own <id>.
            if (tag == QLatin1String("developer")) {
                while (!(xml.isEndElement() && xml.name() == QLatin1String("developer")) && !xml.atEnd()) {
                    xml.readNext();
                    if (xml.isStartElement() && xml.name() == QLatin1String("name") && untranslated(xml) && app.developer.isEmpty())
                        app.developer = xml.readElementText().simplified();
                }
                continue;
            }
            if (tag == QLatin1String("releases")) {
                while (!(xml.isEndElement() && xml.name() == QLatin1String("releases")) && !xml.atEnd()) {
                    xml.readNext();
                    if (xml.isStartElement() && xml.name() == QLatin1String("release")) {
                        if (app.version.isEmpty())
                            app.version = xml.attributes().value(QLatin1String("version")).toString();
                        xml.skipCurrentElement();
                    }
                }
                continue;
            }
            if (tag == QLatin1String("screenshots")) {
                // Per screenshot: the smallest thumbnail at least 400px wide,
                // else the full-size source image.
                QString best;
                int bestWidth = 0;
                QString source;
                while (!(xml.isEndElement() && xml.name() == QLatin1String("screenshots")) && !xml.atEnd()) {
                    xml.readNext();
                    if (xml.isEndElement() && xml.name() == QLatin1String("screenshot")) {
                        const QString chosen = best.isEmpty() ? source : best;
                        if (!chosen.isEmpty() && app.screenshots.size() < 4)
                            app.screenshots << chosen;
                        best.clear();
                        source.clear();
                        bestWidth = 0;
                    } else if (xml.isStartElement() && xml.name() == QLatin1String("image")) {
                        const QString type = xml.attributes().value(QLatin1String("type")).toString();
                        const int width = xml.attributes().value(QLatin1String("width")).toInt();
                        const bool scaled = xml.attributes().hasAttribute(QLatin1String("scale"));
                        const QString url = xml.readElementText().trimmed();
                        if (type == QLatin1String("source"))
                            source = url;
                        else if (!scaled && width >= 400 && (bestWidth == 0 || width < bestWidth)) {
                            best = url;
                            bestWidth = width;
                        }
                    }
                }
                continue;
            }
            if (tag == QLatin1String("provides") || tag == QLatin1String("requires") || tag == QLatin1String("recommends")
                || tag == QLatin1String("suggests") || tag == QLatin1String("replaces")
                || tag == QLatin1String("content_rating") || tag == QLatin1String("custom")) {
                xml.skipCurrentElement();
                continue;
            }
            if (tag == QLatin1String("id")) {
                if (app.id.isEmpty())
                    app.id = xml.readElementText().trimmed();
            } else if (tag == QLatin1String("name") && untranslated(xml)) {
                app.name = xml.readElementText().simplified();
            } else if (tag == QLatin1String("summary") && untranslated(xml)) {
                app.summary = xml.readElementText().simplified();
            } else if (tag == QLatin1String("description") && untranslated(xml)) {
                app.description = readDescription(xml);
            } else if (tag == QLatin1String("pkgname")) {
                app.package = xml.readElementText().trimmed();
            } else if (tag == QLatin1String("bundle") && xml.attributes().value(QLatin1String("type")) == QLatin1String("flatpak")) {
                bundleRef = xml.readElementText().trimmed();
            } else if (tag == QLatin1String("icon")) {
                const QString iconType = xml.attributes().value(QLatin1String("type")).toString();
                const int width = xml.attributes().value(QLatin1String("width")).toInt();
                const bool scaled = xml.attributes().hasAttribute(QLatin1String("scale"));
                const QString value = xml.readElementText().trimmed();
                if (iconType == QLatin1String("cached") && width > 0 && !scaled)
                    cachedIcons.append({width, value});
                else if (iconType == QLatin1String("stock"))
                    app.stockIcon = value;
            } else if (tag == QLatin1String("category")) {
                app.categories << xml.readElementText().trimmed();
            } else if (tag == QLatin1String("keyword") && untranslated(xml)) {
                app.keywords << xml.readElementText().trimmed();
            } else if ((tag == QLatin1String("developer_name") && untranslated(xml)) && app.developer.isEmpty()) {
                app.developer = xml.readElementText().simplified();
            } else if (tag == QLatin1String("project_license")) {
                app.license = xml.readElementText().trimmed();
            } else if (tag == QLatin1String("url") && xml.attributes().value(QLatin1String("type")) == QLatin1String("homepage")) {
                app.homepage = xml.readElementText().trimmed();
            }
        }

        if (kind == AppStreamApp::Kind::Flatpak) {
            // "app/org.example.App/x86_64/stable" -> org.example.App; fall
            // back to the component ID minus any ".desktop".
            app.package = bundleRef.section('/', 1, 1);
            if (app.package.isEmpty()) {
                app.package = app.id;
                if (app.package.endsWith(QLatin1String(".desktop")))
                    app.package.chop(8);
            }
        }
        if (app.name.isEmpty() || app.package.isEmpty())
            continue;
        const QString iconRoot = kind == AppStreamApp::Kind::Flatpak || origin.isEmpty() ? iconBase : iconBase + '/' + origin;
        app.iconPath = pickIcon(cachedIcons, iconRoot);
        out.append(std::move(app));
    }
}

// Debian/Ubuntu DEP-11: a YAML stream of one document per component. Only
// the handful of keys Athena shows are read, by indentation, rather than
// pulling in a YAML library.
void parseDep11Catalog(const QByteArray &data, const QString &iconBase, QVector<AppStreamApp> &out)
{
    const QStringList lines = QString::fromUtf8(data).split('\n');
    QString origin;
    AppStreamApp app;
    QString section;
    QString type;
    QVector<QPair<int, QString>> cachedIcons;
    QString pendingIconName;
    auto flush = [&]() {
        if ((type == QLatin1String("desktop-application") || type == QLatin1String("desktop")) && !app.name.isEmpty()
            && !app.package.isEmpty()) {
            app.origin = origin;
            app.iconPath = pickIcon(cachedIcons, origin.isEmpty() ? QString() : iconBase + '/' + origin);
            out.append(app);
        }
        app = AppStreamApp();
        type.clear();
        section.clear();
        cachedIcons.clear();
    };
    for (const QString &raw : lines) {
        if (raw.startsWith(QLatin1String("---"))) {
            flush();
            continue;
        }
        if (raw.trimmed().isEmpty())
            continue;
        const int leading = raw.indexOf(raw.trimmed());
        const QString line = raw.trimmed();
        const auto value = [&line]() { return line.section(':', 1).trimmed().remove('"').remove('\''); };
        if (leading == 0) {
            section = line.section(':', 0, 0);
            if (section == QLatin1String("Origin"))
                origin = value();
            else if (section == QLatin1String("Type"))
                type = value();
            else if (section == QLatin1String("ID"))
                app.id = value();
            else if (section == QLatin1String("Package"))
                app.package = value();
            continue;
        }
        if (section == QLatin1String("Name") && line.startsWith(QLatin1String("C:")))
            app.name = value();
        else if (section == QLatin1String("Summary") && line.startsWith(QLatin1String("C:")))
            app.summary = value();
        else if (section == QLatin1String("Categories") && line.startsWith('-'))
            app.categories << line.mid(1).trimmed();
        else if (section == QLatin1String("Icon")) {
            if (line.startsWith(QLatin1String("stock:")))
                app.stockIcon = value();
            else if (line.startsWith(QLatin1String("- name:")) || line.startsWith(QLatin1String("name:")))
                pendingIconName = line.section(':', 1).trimmed();
            else if (line.startsWith(QLatin1String("width:")) && !pendingIconName.isEmpty())
                cachedIcons.append({value().toInt(), pendingIconName});
        } else if (section == QLatin1String("Url") && line.startsWith(QLatin1String("homepage:")))
            app.homepage = line.section(':', 1).trimmed();
        else if (section == QLatin1String("ProjectLicense"))
            app.license = value();
    }
    flush();
}

QVector<AppStreamApp> loadAllCatalogs()
{
    QVector<AppStreamApp> apps;

    // Distro catalogs.
    const QVector<QPair<QString, QString>> xmlDirs = {
        {QStringLiteral("/usr/share/swcatalog/xml"), QStringLiteral("/usr/share/swcatalog/icons")},
        {QStringLiteral("/usr/share/app-info/xmls"), QStringLiteral("/usr/share/app-info/icons")},
        {QStringLiteral("/var/cache/swcatalog/xml"), QStringLiteral("/var/cache/swcatalog/icons")},
        {QStringLiteral("/var/cache/app-info/xmls"), QStringLiteral("/var/cache/app-info/icons")},
    };
    for (const auto &[dir, icons] : xmlDirs) {
        const QFileInfoList files = QDir(dir).entryInfoList({QStringLiteral("*.xml"), QStringLiteral("*.xml.gz")}, QDir::Files);
        for (const QFileInfo &file : files)
            parseXmlCatalog(readCatalog(file.absoluteFilePath()), AppStreamApp::Kind::Native, icons, QString(), apps);
    }
    const QStringList yamlDirs = {QStringLiteral("/usr/share/swcatalog/yaml"), QStringLiteral("/var/lib/app-info/yaml")};
    for (const QString &dir : yamlDirs) {
        const QString iconBase = dir.startsWith(QLatin1String("/usr/share/swcatalog")) ? QStringLiteral("/usr/share/swcatalog/icons")
                                                                                       : QStringLiteral("/var/lib/app-info/icons");
        for (const QFileInfo &file : QDir(dir).entryInfoList({QStringLiteral("*.yml"), QStringLiteral("*.yml.gz")}, QDir::Files))
            parseDep11Catalog(readCatalog(file.absoluteFilePath()), iconBase, apps);
    }

    // Flatpak remotes, system and user installation.
    const QStringList installations = {QStringLiteral("/var/lib/flatpak"), QDir::homePath() + QStringLiteral("/.local/share/flatpak")};
    for (const QString &installation : installations) {
        const QString root = installation + QStringLiteral("/appstream");
        for (const QString &remote : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            for (const QString &arch : QDir(root + '/' + remote).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                const QString active = QStringLiteral("%1/%2/%3/active").arg(root, remote, arch);
                const QString xml = QFileInfo::exists(active + QStringLiteral("/appstream.xml")) ? active + QStringLiteral("/appstream.xml")
                                                                                                 : active + QStringLiteral("/appstream.xml.gz");
                if (QFileInfo::exists(xml))
                    parseXmlCatalog(readCatalog(xml), AppStreamApp::Kind::Flatpak, active + QStringLiteral("/icons"), remote, apps);
            }
        }
    }
    return apps;
}

int score(const AppStreamApp &app, const QString &q)
{
    const QString name = app.name.toLower();
    if (name == q || app.package.toLower() == q || app.package.toLower().section('.', -1) == q)
        return 0;
    if (name.startsWith(q))
        return 1;
    if (name.contains(q))
        return 2;
    if (app.package.contains(q, Qt::CaseInsensitive))
        return 3;
    for (const QString &keyword : app.keywords) {
        if (keyword.contains(q, Qt::CaseInsensitive))
            return 4;
    }
    if (app.summary.contains(q, Qt::CaseInsensitive))
        return 5;
    return -1;
}

} // namespace

AppStreamIndex::AppStreamIndex(QObject *parent) : QObject(parent) { }

void AppStreamIndex::load()
{
    const int generation = ++m_generation;
    auto *watcher = new QFutureWatcher<QVector<AppStreamApp>>(this);
    connect(watcher, &QFutureWatcher<QVector<AppStreamApp>>::finished, this, [this, watcher, generation]() {
        QVector<AppStreamApp> apps = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        m_apps = std::move(apps);
        m_nativeByPackage.clear();
        m_flatpakById.clear();
        for (int i = 0; i < m_apps.size(); ++i) {
            QHash<QString, int> &map = m_apps[i].kind == AppStreamApp::Kind::Native ? m_nativeByPackage : m_flatpakById;
            if (!map.contains(m_apps[i].package))
                map.insert(m_apps[i].package, i);
        }
        m_loaded = true;
        emit loaded();
    });
    watcher->setFuture(QtConcurrent::run(&loadAllCatalogs));
}

QVector<const AppStreamApp *> AppStreamIndex::search(const QString &query, int limit) const
{
    const QString q = query.trimmed().toLower();
    if (q.isEmpty())
        return {};
    QVector<QPair<int, const AppStreamApp *>> scored;
    for (const AppStreamApp &app : m_apps) {
        const int s = score(app, q);
        if (s >= 0)
            scored.append({s, &app});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QVector<const AppStreamApp *> result;
    for (int i = 0; i < scored.size() && i < limit; ++i)
        result.append(scored[i].second);
    return result;
}

QVector<const AppStreamApp *> AppStreamIndex::all() const
{
    QVector<const AppStreamApp *> result;
    result.reserve(m_apps.size());
    for (const AppStreamApp &app : m_apps)
        result.append(&app);
    return result;
}

QVector<const AppStreamApp *> AppStreamIndex::inCategory(const QString &category) const
{
    QVector<const AppStreamApp *> result;
    for (const AppStreamApp &app : m_apps) {
        if (app.categories.contains(category))
            result.append(&app);
    }
    return result;
}

const AppStreamApp *AppStreamIndex::findNative(const QString &package) const
{
    const auto it = m_nativeByPackage.constFind(package);
    return it == m_nativeByPackage.constEnd() ? nullptr : &m_apps[it.value()];
}

const AppStreamApp *AppStreamIndex::findFlatpak(const QString &appId) const
{
    const auto it = m_flatpakById.constFind(appId);
    return it == m_flatpakById.constEnd() ? nullptr : &m_apps[it.value()];
}
