#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include "../core/backends/FlatpakBackend.h"

class QTreeWidget;
class QPushButton;
class QLabel;
class DirSizeScanner;

// Browses ~/.var/app data for currently installed Flatpak apps, with
// recursive size scanning, and lets the user clear an app's data (its
// settings/cache/saved files) without uninstalling it. Flatpak-only
// concept, so this takes a FlatpakBackend* directly rather than the
// generic PackageBackend interface.
class UserDataPage : public QWidget {
    Q_OBJECT

public:
    explicit UserDataPage(FlatpakBackend *backend, QWidget *parent = nullptr);

public slots:
    void refresh();

private slots:
    void onAppListLoaded();
    void onSelectionChanged();
    void onClearData();
    void onOpenFolder();
    void onSizeComputed(const QString &path, qint64 bytes);
    void onScanFinished();

private:
    FlatpakBackend *m_backend;

    QTreeWidget *m_tree;
    QPushButton *m_refreshButton;
    QPushButton *m_openButton;
    QPushButton *m_clearButton;
    QLabel *m_totalLabel;

    DirSizeScanner *m_scanner = nullptr;

    QFutureWatcher<QVector<PackageInfo>> m_appListWatcher;
};
