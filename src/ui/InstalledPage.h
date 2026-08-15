#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include "../core/PackageBackend.h"

class QLineEdit;
class QLabel;
class QPushButton;
class PackageBrowser;

class InstalledPage : public QWidget {
    Q_OBJECT

public:
    explicit InstalledPage(PackageBackend *backend, QWidget *parent = nullptr);

public slots:
    void refresh();

private:
    void onLoaded();
    void onCleanClicked();
    void setBusy(bool busy);

    PackageBackend *m_backend;
    PackageBrowser *m_browser;
    QLineEdit *m_filterEdit;
    QLabel *m_statusLabel;
    QPushButton *m_refreshButton;
    QPushButton *m_cleanButton;
    QFutureWatcher<QVector<PackageInfo>> m_watcher;
};
