#pragma once

#include <QWidget>

#include "../core/PackageBackend.h"

class QTableView;
class PackageTableModel;
class QSortFilterProxyModel;
class QLabel;
class QPushButton;
class QModelIndex;
class QPoint;

// Synaptic-style package list: a checkbox-selectable table (Name /
// Installed Version / Latest Version / Description) with a details panel
// below showing the highlighted row's full description, plus action
// buttons and a matching right-click menu. Shared by InstalledPage,
// SearchPage, GroupsPage, and UpdatesPage so the checkbox multi-select and
// action wiring isn't duplicated across all of them.
class PackageBrowser : public QWidget {
    Q_OBJECT

public:
    // InstallRemove: Install Selected / Uninstall Selected, for browsing
    // packages that may or may not be installed.
    // Updates: a single Update Selected action, for a list where every
    // row is already installed and represents an available upgrade.
    enum class Mode { InstallRemove, Updates };

    explicit PackageBrowser(PackageBackend *backend, Mode mode, QWidget *parent = nullptr);

    void setPackages(const QVector<PackageInfo> &packages);
    void setBusy(bool busy);
    bool isBusy() const { return m_busy; }

    // Filters visible rows by name/description substring (case-insensitive).
    void setFilterText(const QString &text);

    PackageTableModel *model() const { return m_model; }

signals:
    // Emitted after a successful install/uninstall/update so the owning
    // page can reload its data and call setPackages() again.
    void refreshRequested();

private slots:
    void onCheckedChanged();
    void onCurrentRowChanged(const QModelIndex &current, const QModelIndex &previous);
    void onSelectionChanged();
    void installChecked();
    void uninstallUnchecked();
    void reinstallSelected();
    void updateChecked();
    void showContextMenu(const QPoint &pos);

private:
    void updateDescriptionPanel(const PackageInfo *pkg);
    // Installed packages among the currently highlighted table rows —
    // reinstall targets row selection, not the checkbox (which represents
    // desired install state, not "selected for an action").
    QVector<PackageInfo> selectedInstalledPackages() const;
    void runInstall(const QStringList &names);
    void runRemove(const QStringList &names);
    void runReinstall(const QStringList &names);
    void runUpgrade(const QStringList &names);

    PackageBackend *m_backend;
    Mode m_mode;
    PackageTableModel *m_model;
    QSortFilterProxyModel *m_proxyModel;
    QTableView *m_tableView;
    QPushButton *m_selectAllButton;
    QPushButton *m_selectNoneButton;
    QPushButton *m_installButton;
    QPushButton *m_uninstallButton;
    QPushButton *m_reinstallButton;
    QPushButton *m_updateButton;
    QLabel *m_detailTitle;
    QLabel *m_detailMeta;
    QLabel *m_detailDescription;
    bool m_busy = false;
};
