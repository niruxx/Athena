#include "CatalogHub.h"

#include <algorithm>
#include <memory>

#include <QFutureWatcher>
#include <QSet>
#include <QtConcurrent/QtConcurrent>

namespace {

using Batch = QVector<QVector<PackageInfo>>; // per source

// Runs `query` on every source in parallel, then hands all the results to
// `done` on the UI thread once the last one finishes. Results from a
// refresh that's been superseded (generation moved on) are dropped.
template <typename Query, typename Done>
void loadAll(QObject *context, const QVector<CatalogSource> &sources, int &pending, Query query, Done done)
{
    pending = sources.size();
    auto results = std::make_shared<Batch>(sources.size());
    auto remaining = std::make_shared<int>(sources.size());
    for (int i = 0; i < sources.size(); ++i) {
        auto *watcher = new QFutureWatcher<QVector<PackageInfo>>(context);
        QObject::connect(watcher, &QFutureWatcher<QVector<PackageInfo>>::finished, context,
                         [watcher, results, remaining, i, done]() {
                             (*results)[i] = watcher->result();
                             watcher->deleteLater();
                             if (--*remaining == 0)
                                 done(*results);
                         });
        PackageBackend *backend = sources[i].backend;
        watcher->setFuture(QtConcurrent::run([backend, query]() { return query(backend); }));
    }
}

} // namespace

CatalogHub::CatalogHub(QVector<CatalogSource> sources, QObject *parent)
    : QObject(parent), m_sources(std::move(sources)), m_appIndex(new AppStreamIndex(this))
{
    m_appIndex->load();
    connect(m_appIndex, &AppStreamIndex::loaded, this, [this]() {
        buildInstalled();
        if (!m_updatesBatch.isEmpty())
            buildUpdates();
    });
}

int CatalogHub::nativeSource() const
{
    for (int i = 0; i < m_sources.size(); ++i) {
        const QString &key = m_sources[i].key;
        if (key != QLatin1String("aur") && key != QLatin1String("flatpak") && key != QLatin1String("snap"))
            return i;
    }
    return -1;
}

QVector<CatalogItem> CatalogHub::itemsFor(const QVector<const AppStreamApp *> &apps) const
{
    const int native = nativeSource();
    const int flatpak = indexOf(QStringLiteral("flatpak"));
    QVector<CatalogItem> items;
    items.reserve(apps.size());
    for (const AppStreamApp *app : apps) {
        const int source = app->kind == AppStreamApp::Kind::Flatpak ? flatpak : native;
        if (source >= 0)
            items.append(Catalog::fromAppStream(source, *app));
    }
    return items;
}

int CatalogHub::indexOf(const QString &key) const
{
    for (int i = 0; i < m_sources.size(); ++i) {
        if (m_sources[i].key == key)
            return i;
    }
    return -1;
}

void CatalogHub::refreshAll()
{
    refreshInstalled();
    refreshUpdates();
}

void CatalogHub::refreshInstalled()
{
    if (m_sources.isEmpty())
        return;
    const int generation = ++m_installedGeneration;
    loadAll(this, m_sources, m_installedPending, [](PackageBackend *b) { return b->listInstalled(); },
            [this, generation](const Batch &batch) {
                if (generation != m_installedGeneration)
                    return;
                m_installedPending = 0;
                m_installedBatch = batch;
                buildInstalled();
            });
}

void CatalogHub::enrich(CatalogItem &item) const
{
    if (!m_appIndex->isLoaded() || item.variants.size() != 1)
        return;
    CatalogVariant &variant = item.variants.first();
    const QString key = m_sources[variant.source].key;
    const AppStreamApp *app = key == QLatin1String("flatpak") ? m_appIndex->findFlatpak(variant.info.name)
        : variant.source == nativeSource()                    ? m_appIndex->findNative(variant.info.name)
                                                              : nullptr;
    if (!app)
        return;
    const CatalogItem fromIndex = Catalog::fromAppStream(variant.source, *app);
    if (!item.isApp) {
        item.isApp = true;
        item.title = app->name;
        item.subtitle = app->summary;
    }
    if (item.icon.isNull())
        item.icon = fromIndex.icon;
    if (item.subtitle.isEmpty())
        item.subtitle = app->summary;
    item.screenshots = app->screenshots;
    if (variant.info.longDescription.size() < app->description.size())
        variant.info.longDescription = app->description;
    if (variant.info.homepageUrl.isEmpty())
        variant.info.homepageUrl = app->homepage;
}

void CatalogHub::buildInstalled()
{
    const Batch &batch = m_installedBatch;
    if (batch.isEmpty())
        return;
    // pacman -Q also lists AUR (foreign) packages; show those once, under
    // the AUR source.
    QSet<QString> aurNames;
    const int aur = indexOf(QStringLiteral("aur"));
    if (aur >= 0) {
        for (const PackageInfo &pkg : batch[aur])
            aurNames.insert(pkg.name);
    }

    QVector<CatalogItem> items;
    for (int s = 0; s < batch.size(); ++s) {
        const bool native = s != aur && m_sources[s].key != QLatin1String("flatpak") && m_sources[s].key != QLatin1String("snap");
        for (const PackageInfo &pkg : batch[s]) {
            if (native && aurNames.contains(pkg.name))
                continue;
            CatalogItem item = Catalog::fromPackage(m_sources, s, pkg);
            // Nothing lands from the AUR as a dependency nobody asked for:
            // each one was a deliberate install, so it belongs under "Apps
            // only" even without a .desktop file (CLI tools like yay).
            // Debug symbol packages aren't, though.
            if (s == aur && !pkg.name.endsWith(QLatin1String("-debug")))
                item.isApp = true;
            enrich(item);
            items.append(item);
        }
    }
    items = Catalog::mergeApps(items);
    std::sort(items.begin(), items.end(), [](const CatalogItem &a, const CatalogItem &b) {
        return QString::compare(a.title, b.title, Qt::CaseInsensitive) < 0;
    });
    m_installed = std::move(items);
    emit installedChanged();
}

void CatalogHub::refreshUpdates()
{
    if (m_sources.isEmpty())
        return;
    const int generation = ++m_updatesGeneration;
    loadAll(this, m_sources, m_updatesPending, [](PackageBackend *b) { return b->listUpdates(); },
            [this, generation](const Batch &batch) {
                if (generation != m_updatesGeneration)
                    return;
                m_updatesPending = 0;
                m_updatesBatch = batch;
                buildUpdates();
            });
}

void CatalogHub::buildUpdates()
{
    QVector<CatalogItem> items;
    for (int s = 0; s < m_updatesBatch.size(); ++s) {
        for (const PackageInfo &pkg : m_updatesBatch[s]) {
            CatalogItem item = Catalog::fromPackage(m_sources, s, pkg);
            enrich(item);
            items.append(item);
        }
    }
    // Apps people recognize first; runtimes and libraries after.
    std::stable_sort(items.begin(), items.end(), [](const CatalogItem &a, const CatalogItem &b) { return a.isApp && !b.isApp; });
    m_updates = std::move(items);
    emit updatesChanged();
}
