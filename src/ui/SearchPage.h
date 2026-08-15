#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include "../core/PackageBackend.h"

class QLineEdit;
class QLabel;
class QPushButton;
class PackageBrowser;

class SearchPage : public QWidget {
    Q_OBJECT

public:
    explicit SearchPage(PackageBackend *backend, QWidget *parent = nullptr);

private slots:
    void runSearch();
    void onSearchFinished();

private:
    PackageBackend *m_backend;
    PackageBrowser *m_browser;
    QLineEdit *m_queryEdit;
    QPushButton *m_searchButton;
    QLabel *m_statusLabel;
    QFutureWatcher<QVector<PackageInfo>> m_watcher;
};
