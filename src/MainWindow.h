#pragma once

#include <memory>

#include <QMainWindow>
#include <QMap>
#include <QString>
#include <QSystemTrayIcon>
#include <QVector>

#include "core/PackageBackend.h"
#include "core/PackageInfo.h"
#include "core/backends/AurBackend.h"
#include "core/backends/FlatpakBackend.h"

class QAction;
class QLabel;
class QMenu;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QTimer;
class CatalogHub;
class DetailsSheet;
class FramelessHelper;
class DiscoverPage;
class HomePage;
class NavigationRail;
class Snackbar;
class SourcesPage;

// The Material You shell: a navigation rail (Home, Discover, Installed,
// Updates, Sources, Tools, Settings) beside a rounded content pane, with
// the details side sheet and snackbar layered over both. Pages work
// across every source through one CatalogHub.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onUpdatesChanged();
    void updateTrayIconState(int totalUpdates);
    // Installs every pending update across all sources in one
    // confirm-then-run transaction; AUR updates (with PKGBUILD review on)
    // open for review once the rest finish.
    void updateAll();
    void applyDisableGroupView(bool disabled);

private:
    void buildSources();
    QWidget *buildSourceTabs();
    void buildShell();
    void setupActions();
    void setupTray();
    void goToPage(const QString &key);
    void reviewAurUpdates();
    void reloadAllPackageInformation();
    void checkForApplicationUpdates();
    void showAboutDialog();
    void showMessage(const QString &text);
    // Switches between Athena's own title bar and the desktop's.
    void applyTitleBarMode(bool custom);
    // The 1px outline a frameless window needs (no desktop decoration to
    // separate it from what's behind), dropped when maximized.
    void updateWindowFrame();
    QVector<PackageBackend *> allBackends() const;

    std::unique_ptr<PackageBackend> m_backend;
    // Concrete types: PermissionsPage and the details sheet need
    // Flatpak-only methods, AurPkgbuildPage needs fetchPkgbuild().
    std::unique_ptr<FlatpakBackend> m_flatpakBackend;
    std::unique_ptr<PackageBackend> m_snapBackend;
    std::unique_ptr<AurBackend> m_aurBackend;

    CatalogHub *m_hub = nullptr;
    NavigationRail *m_rail = nullptr;
    // Narrow windows: Material's bottom navigation bar instead of the rail,
    // with the first kBottomDestinations pages (Tools and Settings move to
    // the ⋮ menu).
    NavigationRail *m_bottomBar = nullptr;
    static constexpr int kBottomDestinations = 5;
    QStackedWidget *m_pages = nullptr;
    QLabel *m_pageTitle = nullptr;
    QMenu *m_overflowMenu = nullptr;
    FramelessHelper *m_frameless = nullptr;
    QWidget *m_windowFrame = nullptr;
    QWidget *m_titleBar = nullptr;
    QWidget *m_windowControls = nullptr;
    QPushButton *m_maximizeButton = nullptr;
    QStringList m_pageKeys;
    QStringList m_pageTitles;
    DetailsSheet *m_sheet = nullptr;
    Snackbar *m_snackbar = nullptr;
    DiscoverPage *m_discover = nullptr;
    HomePage *m_home = nullptr;
    SourcesPage *m_sourcesPage = nullptr;
    class UpdateBannerWidget *m_updateBanner = nullptr;

    // The system source's classic tabs, and its Groups tab — kept alive
    // while AppSettings::disableGroupView() has it removed from the tab bar.
    QTabWidget *m_systemTabs = nullptr;
    QWidget *m_systemGroupsTab = nullptr;

    QSystemTrayIcon *m_trayIcon = nullptr;
    QAction *m_trayUpdateAction = nullptr;
    QTimer *m_updateCheckTimer = nullptr;
    QMap<PackageBackend *, QStringList> m_pendingUpdatesByBackend;
};
