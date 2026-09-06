#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include "../core/backends/FlatpakBackend.h"

class QTreeWidget;
class QPushButton;
class QLabel;
class QCheckBox;
class DirSizeScanner;

// Finds data left behind under ~/.var/app (and, optionally, orphaned
// permission-override files) by Flatpak apps that are no longer
// installed — e.g. removed via a different tool, or without clearing
// their data first. Flatpak-only concept, so this takes a FlatpakBackend*
// directly rather than the generic PackageBackend interface.
class LeftoverDataPage : public QWidget {
    Q_OBJECT

public:
    explicit LeftoverDataPage(FlatpakBackend *backend, QWidget *parent = nullptr);

public slots:
    void refresh();

private slots:
    void onAppListLoaded();
    void onSelectionChanged();
    void onDeleteSelected();
    void onDeleteAll();
    void onOpenFolder();
    void onSizeComputed(const QString &path, qint64 bytes);
    void onScanFinished();

private:
    void deletePaths(const QStringList &paths);

    FlatpakBackend *m_backend;

    QTreeWidget *m_tree;
    QPushButton *m_refreshButton;
    QPushButton *m_openButton;
    QPushButton *m_deleteButton;
    QPushButton *m_deleteAllButton;
    QCheckBox *m_includeOverridesCheck;
    QLabel *m_totalLabel;

    DirSizeScanner *m_scanner = nullptr;

    QFutureWatcher<QVector<PackageInfo>> m_appListWatcher;
};
