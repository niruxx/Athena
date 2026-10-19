#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include "../core/PackageBackend.h"
#include "../core/PackageGroupInfo.h"

class QButtonGroup;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class PackageBrowser;

// Result of selecting a group: its description, plus the flat list of
// packages it contains — for a meta-group (a dnf environment) this means
// every sub-group's packages unioned together, since the package browser
// always shows an actual package list rather than a further level of groups.
struct GroupContentsResult {
    QString description;
    QVector<PackageInfo> packages;
};

// The Groups page: the native package manager's package groups (pacman
// groups, dnf groups and environments, tasksel tasks), searchable, and for
// the selected group its packages — filterable by name and by installed
// state, installable or removable one by one, or all of the group's
// missing packages at once.
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
    void filterGroups(const QString &text);
    // Shows m_packages through the installed-state filter.
    void showPackages();
    void installMissing();
    void removeInstalled();

    PackageBackend *m_backend;
    QLineEdit *m_groupSearch;
    QTreeWidget *m_tree;
    QLabel *m_statusLabel;
    QPushButton *m_refreshButton;

    QStackedWidget *m_detailStack;
    QLabel *m_emptyState;
    QLabel *m_categoryTitle;
    QLabel *m_categoryDescription;
    QLabel *m_groupStats;
    QPushButton *m_installMissingButton;
    QPushButton *m_removeInstalledButton;
    QLineEdit *m_packageSearch;
    QButtonGroup *m_stateFilter;
    PackageBrowser *m_browser;

    QVector<PackageInfo> m_packages;
    QFutureWatcher<QVector<PackageGroupInfo>> m_listWatcher;
    QFutureWatcher<GroupContentsResult> m_contentsWatcher;
};
