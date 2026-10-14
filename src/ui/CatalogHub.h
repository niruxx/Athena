#pragma once

#include <QObject>
#include <QVector>

#include "Catalog.h"

// Owns the cross-source installed and update lists the redesigned pages
// share (Home, Installed, Updates, and the tray), so each is loaded once —
// every source in parallel — instead of once per page.
class CatalogHub : public QObject {
    Q_OBJECT

public:
    CatalogHub(QVector<CatalogSource> sources, QObject *parent = nullptr);

    const QVector<CatalogSource> &sources() const { return m_sources; }
    // Stable pointer for delegates and the details sheet.
    const QVector<CatalogSource> *sourcesPtr() const { return &m_sources; }
    int indexOf(const QString &key) const;
    // The distro package manager's source (pacman/dnf/apt), or -1.
    int nativeSource() const;

    // Every app the sources offer, installed or not (see AppStreamIndex).
    AppStreamIndex *appIndex() const { return m_appIndex; }
    // The index's apps as catalog items, routed to the matching source
    // (distro catalogs to the native source, Flatpak remotes to Flatpak);
    // apps from a source Athena isn't managing are left out.
    QVector<CatalogItem> itemsFor(const QVector<const AppStreamApp *> &apps) const;

    // Installed apps and packages, apps merged across sources.
    const QVector<CatalogItem> &installed() const { return m_installed; }
    // One item per pending update (never merged: each is its own transaction).
    const QVector<CatalogItem> &updates() const { return m_updates; }
    bool installedLoading() const { return m_installedPending > 0; }
    bool updatesLoading() const { return m_updatesPending > 0; }

public slots:
    void refreshInstalled();
    void refreshUpdates();
    void refreshAll();

signals:
    void installedChanged();
    void updatesChanged();

private:
    // Builds the item lists from the last raw results — again once the
    // AppStream index loads, to pick up its names, icons, and screenshots.
    void buildInstalled();
    void buildUpdates();
    // Fills in an item from the AppStream index (title, summary, icon,
    // screenshots, description) where it has nothing better already.
    void enrich(CatalogItem &item) const;

    QVector<QVector<PackageInfo>> m_installedBatch;
    QVector<QVector<PackageInfo>> m_updatesBatch;
    QVector<CatalogSource> m_sources;
    AppStreamIndex *m_appIndex;
    QVector<CatalogItem> m_installed;
    QVector<CatalogItem> m_updates;
    int m_installedPending = 0;
    int m_updatesPending = 0;
    int m_installedGeneration = 0;
    int m_updatesGeneration = 0;
};
