#pragma once

#include <memory>

#include <QMainWindow>
#include <QString>

#include "core/PackageBackend.h"

class QTabWidget;
class QStackedWidget;
class QComboBox;
class PackageBrowser;

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
    std::unique_ptr<PackageBackend> m_flatpakBackend;
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
};
