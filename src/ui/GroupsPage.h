#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include "../core/PackageBackend.h"
#include "../core/PackageGroupInfo.h"

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;
class PackageBrowser;

// Result of selecting a category: its description, plus the flat list of
// packages it contains — for a meta-group this means every sub-group's
// packages unioned together, since the package browser always shows an
// actual package list rather than a further level of categories.
struct GroupContentsResult {
    QString description;
    QVector<PackageInfo> packages;
};

class GroupsPage : public QWidget {
    Q_OBJECT

public:
    explicit GroupsPage(PackageBackend *backend, QWidget *parent = nullptr);

    PackageBrowser *browser() const { return m_browser; }

public slots:
    void refresh();

private slots:
    void onGroupsLoaded();
    void onGroupSelected(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void onGroupContentsLoaded();

private:
    PackageBackend *m_backend;
    QTreeWidget *m_tree;
    QLabel *m_categoryTitle;
    QLabel *m_categoryDescription;
    PackageBrowser *m_browser;
    QLabel *m_statusLabel;
    QPushButton *m_refreshButton;

    QFutureWatcher<QVector<PackageGroupInfo>> m_listWatcher;
    QFutureWatcher<GroupContentsResult> m_contentsWatcher;
};
