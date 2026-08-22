#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include "../core/PackageBackend.h"

class QLabel;
class QPushButton;
class PackageBrowser;

class UpdatesPage : public QWidget {
    Q_OBJECT

public:
    explicit UpdatesPage(PackageBackend *backend, QWidget *parent = nullptr);

    PackageBrowser *browser() const { return m_browser; }

public slots:
    void refresh();

private:
    void onLoaded();

    PackageBackend *m_backend;
    PackageBrowser *m_browser;
    QLabel *m_statusLabel;
    QPushButton *m_refreshButton;
    QFutureWatcher<QVector<PackageInfo>> m_watcher;
};
