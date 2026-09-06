#pragma once

#include <memory>
#include <utility>

#include <QMainWindow>
#include <QMap>
#include <QString>
#include <QSystemTrayIcon>
#include <QVector>

#include "core/PackageBackend.h"
#include "core/PackageInfo.h"
#include "core/backends/FlatpakBackend.h"

class QTabWidget;
class QStackedWidget;
class QComboBox;
class QTimer;
class QAction;
class PackageBrowser;
class GroupsPage;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    // Recomputes the bottom-bar package counts from whichever
    // package-listing page (Installed/Updates/Search/Groups) is currently
    // visible, or clears them if the active group isn't showing a package
    // list (Repositories, History).
    void updateStatusBarStats();
    // Moves m_groupCombo into the corner of whichever group's QTabWidget
    // is currently visible, so it shares the tab bar's row instead of
    // sitting in a row of its own.
    void placeGroupComboInCornerWidget();

    // Background poll (on the interval from AppSettings) of every active
    // backend's listUpdates(), feeding the tray icon's visibility/tooltip
    // and the "Update" tray action's target package list.
    void pollForTrayUpdates();
    void updateTrayIconState(int totalUpdates);
    // Installs every update found by the last pollForTrayUpdates(), across
    // all backends, in one confirm-then-run transaction.
    void updateAllFromTray();
    void applyDisableGroupView(bool disabled);

private:
    void setupMenuBar();

    // Which backend/group (System, Flatpak, or Snap) the top-right
    // dropdown is currently showing, so menu actions (Reload, Clean
    // Unused Dependencies, Find, ...) apply to whatever the user is
    // actually looking at rather than a fixed backend.
    QTabWidget *currentGroupTabs() const;
    PackageBackend *currentGroupBackend() const;
    // Switches the current group's sub-tab widget to the tab named
    // `tabText` (e.g. "Search", "Updates"), if it has one.
    void focusGroupTab(const QString &tabText);
    // The PackageBrowser behind whichever sub-tab is currently showing
    // (Installed/Updates/Search/Groups), or nullptr for sub-tabs that
    // aren't a package list (Repositories, History) or when no group is
    // active at all.
    PackageBrowser *currentPackageBrowser() const;

    void reloadAllPackageInformation();
    void checkForApplicationUpdates();
    void showAboutDialog();

    std::unique_ptr<PackageBackend> m_backend;
    // Kept as the concrete type (not PackageBackend, unlike the other two)
    // since PermissionsPage needs Flatpak-only methods that aren't part of
    // the shared PackageBackend interface.
    std::unique_ptr<FlatpakBackend> m_flatpakBackend;
    std::unique_ptr<PackageBackend> m_snapBackend;
    class UpdateBannerWidget *m_updateBanner = nullptr;

    // The System/Flatpak/Snap groups, switched via m_groupCombo (top-right
    // of the window) instead of a top-level tab bar. Settings lives in its
    // own PreferencesDialog (Settings > Preferences) rather than a tab.
    QStackedWidget *m_groupStack = nullptr;
    QComboBox *m_groupCombo = nullptr;
    QTabWidget *m_systemTabs = nullptr;
    QTabWidget *m_flatpakTabs = nullptr;
    QTabWidget *m_snapTabs = nullptr;
    class QLabel *m_statsLabel = nullptr;

    // Whichever QTabWidget m_groupCombo is currently a corner widget of, so
    // placeGroupComboInCornerWidget() can explicitly detach it from there
    // before attaching it elsewhere. QTabWidget::setCornerWidget() is a
    // no-op when asked to set the same widget it already believes is its
    // corner widget — without this, a tab widget that hosted the combo
    // once (e.g. at startup) keeps thinking it still does, so handing the
    // combo back to it later silently fails and the combo is left an
    // invisible child of whichever tab widget last actually held it.
    QTabWidget *m_groupComboHost = nullptr;

    // Kept alive (and parented to m_systemTabs) even while
    // AppSettings::disableGroupView() has it removed from the tab bar, so
    // toggling the setting back on doesn't need to recreate the page.
    GroupsPage *m_systemGroupsPage = nullptr;

    QSystemTrayIcon *m_trayIcon = nullptr;
    QAction *m_trayUpdateAction = nullptr;
    QTimer *m_updateCheckTimer = nullptr;
    QMap<PackageBackend *, QVector<PackageInfo>> m_pendingUpdatesByBackend;
};
