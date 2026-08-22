#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include "../core/PackageBackend.h"
#include "../core/PackageGroupInfo.h"

class QLineEdit;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class PackageBrowser;

class InstalledPage : public QWidget {
    Q_OBJECT

public:
    explicit InstalledPage(PackageBackend *backend, QWidget *parent = nullptr);

    PackageBrowser *browser() const { return m_browser; }

public slots:
    void refresh();

private:
    void onLoaded();
    void onCleanClicked();
    void setBusy(bool busy);

    void refreshCategories();
    void onCategoriesLoaded();
    void onCategorySelected(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void onCategoryContentsLoaded();
    // Re-applies the current category filter (if any) on top of the full
    // installed-package list and pushes the result into the browser.
    void applyCategoryFilter();

    PackageBackend *m_backend;
    PackageBrowser *m_browser;
    QTreeWidget *m_categoryTree;
    QLineEdit *m_filterEdit;
    QLabel *m_statusLabel;
    QPushButton *m_refreshButton;
    QPushButton *m_cleanButton;
    QFutureWatcher<QVector<PackageInfo>> m_watcher;
    QFutureWatcher<QVector<PackageGroupInfo>> m_categoriesWatcher;
    QFutureWatcher<QVector<PackageInfo>> m_categoryContentsWatcher;
    QVector<PackageInfo> m_allPackages;
    bool m_categoryFilterActive = false;
};
