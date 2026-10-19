#include "MainWindow.h"

#include <algorithm>
#include <functional>

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QDesktopServices>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "core/AppSettings.h"
#include "core/BackendFactory.h"
#include "core/GitHubReleaseChecker.h"
#include "core/SystemTools.h"
#include "core/Version.h"
#include "core/backends/PacmanBackend.h"
#include "core/backends/SnapBackend.h"
#include "ui/AurPkgbuildPage.h"
#include "ui/AurReviewDialog.h"
#include "ui/BackupRestorePage.h"
#include "ui/CatalogHub.h"
#include "ui/DetailsSheet.h"
#include "ui/FirstRunDialog.h"
#include "ui/FramelessHelper.h"
#include "ui/GroupsPage.h"
#include "ui/HistoryPage.h"
#include "ui/LeftoverDataPage.h"
#include "ui/M3Dialog.h"
#include "ui/M3Widgets.h"
#include "ui/MaterialTheme.h"
#include "ui/Motion.h"
#include "ui/NavigationRail.h"
#include "ui/PackageActions.h"
#include "ui/PermissionsPage.h"
#include "ui/RedesignPages.h"
#include "ui/RepositoriesPage.h"
#include "ui/SettingsPage.h"
#include "ui/SnapManagePage.h"
#include "ui/SnapshotsPage.h"
#include "ui/TransactionConfirmDialog.h"
#include "ui/ToolsDialogs.h"
#include "ui/ToolsPages.h"
#include "ui/UpdateBannerWidget.h"
#include "ui/UserBackupRestorePage.h"
#include "ui/UserCleanupPage.h"
#include "ui/UserDataPage.h"

namespace {
const char *kProjectUrl = "https://github.com/niruxx/Athena";
const char *kIssuesUrl = "https://github.com/niruxx/Athena/issues";

// backendName() returns a descriptive label like "DNF (Fedora / RHEL)";
// source chips just want the bare tool name ("dnf").
QString shortBackendName(const QString &backendName)
{
    const int parenIndex = backendName.indexOf(" (");
    return (parenIndex >= 0 ? backendName.left(parenIndex) : backendName).toLower();
}

// A tab whose real page is only built the first time it's shown, so the
// classic per-source pages (several of which load package lists in their
// constructors) don't all query their backends at startup.
class LazyPage : public QWidget {
public:
    LazyPage(std::function<QWidget *(QWidget *)> factory, QWidget *parent) : QWidget(parent), m_factory(std::move(factory))
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 8, 0, 0);
    }

protected:
    void showEvent(QShowEvent *event) override
    {
        if (m_factory) {
            layout()->addWidget(m_factory(this));
            m_factory = nullptr;
        }
        QWidget::showEvent(event);
    }

private:
    std::function<QWidget *(QWidget *)> m_factory;
};

QTabWidget *sourceTabs(QWidget *parent)
{
    auto *tabs = new QTabWidget(parent);
    tabs->setDocumentMode(true);
    return tabs;
}

template <typename Page, typename Backend>
LazyPage *lazy(Backend *backend, QWidget *parent)
{
    return new LazyPage([backend](QWidget *host) { return new Page(backend, host); }, parent);
}

LazyPage *lazyRepositories(PackageBackend *backend, const QString &label, QWidget *parent)
{
    return new LazyPage([backend, label](QWidget *host) { return new RepositoriesPage({{backend, label}}, host); }, parent);
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("Athena"));
    resize(1260, 820);
    setMinimumSize(420, 520);
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        move(available.x() + (available.width() - width()) / 2, available.y() + (available.height() - height()) / 2);
    }

    buildSources();
    buildShell();
    setupActions();
    setupTray();

    // The title row and the rail's empty space are the window's drag areas.
    m_frameless = new FramelessHelper(this);
    m_frameless->addDragArea(m_titleBar);
    m_frameless->addDragArea(m_rail);
    applyTitleBarMode(AppSettings::instance().useCustomTitleBar());
    connect(&AppSettings::instance(), &AppSettings::useCustomTitleBarChanged, this, &MainWindow::applyTitleBarMode);

    connect(m_hub, &CatalogHub::updatesChanged, this, &MainWindow::onUpdatesChanged);
    m_hub->refreshAll();

    switch (AppSettings::instance().startupTab()) {
    case StartupTab::Discover:
        goToPage(QStringLiteral("discover"));
        break;
    case StartupTab::Installed:
        goToPage(QStringLiteral("installed"));
        break;
    case StartupTab::Updates:
        goToPage(QStringLiteral("updates"));
        break;
    case StartupTab::Sources:
        goToPage(QStringLiteral("sources"));
        break;
    case StartupTab::Tools:
        goToPage(QStringLiteral("tools"));
        break;
    case StartupTab::Home:
        goToPage(QStringLiteral("home"));
        break;
    }

    if (AppSettings::instance().checkForAppUpdatesOnStartup()) {
        auto *checker = new GitHubReleaseChecker(this);
        connect(checker, &GitHubReleaseChecker::updateAvailable, this,
                [this](const QString &version, const QString &htmlUrl) { m_updateBanner->showUpdate(version, htmlUrl); });
        checker->checkForUpdate();
    }

    if (!AppSettings::instance().hasCompletedFirstRun()) {
        const QString backendName = m_backend ? m_backend->backendName() : QString();
        // Deferred so the main window is already visible behind it.
        QTimer::singleShot(0, this, [this, backendName]() {
            FirstRunDialog dialog(backendName, this);
            dialog.exec();
        });
    }
}

void MainWindow::buildSources()
{
    m_backend = BackendFactory::createForHostSystem();

    QVector<CatalogSource> sources;
    if (m_backend)
        sources.append({m_backend.get(), shortBackendName(m_backend->backendName()), shortBackendName(m_backend->backendName()),
                        QStringLiteral("inventory_2")});

    // The AUR only makes sense on top of pacman.
    if (dynamic_cast<PacmanBackend *>(m_backend.get())) {
        auto aur = std::make_unique<AurBackend>();
        if (aur->isAvailable()) {
            m_aurBackend = std::move(aur);
            sources.append({m_aurBackend.get(), QStringLiteral("aur"), tr("AUR"), QStringLiteral("construction")});
        }
    }
    if (auto flatpak = std::make_unique<FlatpakBackend>(); flatpak->isAvailable()) {
        m_flatpakBackend = std::move(flatpak);
        sources.append({m_flatpakBackend.get(), QStringLiteral("flatpak"), tr("Flatpak"), QStringLiteral("deployed_code")});
    }
    if (auto snap = std::make_unique<SnapBackend>(); snap->isAvailable()) {
        m_snapBackend = std::move(snap);
        sources.append({m_snapBackend.get(), QStringLiteral("snap"), tr("Snap"), QStringLiteral("widgets")});
    }

    m_hub = new CatalogHub(sources, this);
}

QWidget *MainWindow::buildSourceTabs()
{
    m_sourcesPage = new SourcesPage(this);

    if (m_backend) {
        PackageBackend *native = m_backend.get();
        m_systemTabs = sourceTabs(m_sourcesPage);
        m_systemTabs->addTab(lazyRepositories(native, tr("System"), m_systemTabs), tr("Repositories"));
        const QString nativeKey = shortBackendName(native->backendName());
        m_systemTabs->addTab(new LazyPage([native, nativeKey](QWidget *host) { return new HistoryPage(native, host, nativeKey); },
                                          m_systemTabs),
                             tr("History"));
        if (SystemTools::mirrorRankingAvailable(nativeKey))
            m_systemTabs->addTab(new LazyPage([nativeKey](QWidget *host) { return new MirrorsPage(nativeKey, host); }, m_systemTabs),
                                 tr("Mirrors"));
        m_sourcesPage->addSource(shortBackendName(native->backendName()), QStringLiteral("inventory_2"), m_systemTabs);
    }
    if (m_aurBackend) {
        AurBackend *aur = m_aurBackend.get();
        QTabWidget *tabs = sourceTabs(m_sourcesPage);
        tabs->addTab(lazy<AurPkgbuildPage>(aur, tabs), tr("PKGBUILD"));
        tabs->addTab(lazy<HistoryPage>(static_cast<PackageBackend *>(aur), tabs), tr("History"));
        m_sourcesPage->addSource(tr("AUR"), QStringLiteral("construction"), tabs);
    }
    if (m_flatpakBackend) {
        FlatpakBackend *flatpak = m_flatpakBackend.get();
        QTabWidget *tabs = sourceTabs(m_sourcesPage);
        tabs->addTab(lazyRepositories(flatpak, tr("Flatpak"), tabs), tr("Remotes"));
        tabs->addTab(new LazyPage([flatpak](QWidget *host) { return new HistoryPage(flatpak, host, QStringLiteral("flatpak")); }, tabs),
                     tr("History"));
        m_sourcesPage->addSource(tr("Flatpak"), QStringLiteral("deployed_code"), tabs);
    }
    if (m_snapBackend) {
        PackageBackend *snap = m_snapBackend.get();
        QTabWidget *tabs = sourceTabs(m_sourcesPage);
        tabs->addTab(new LazyPage([](QWidget *host) { return new SnapManagePage(host); }, tabs), tr("Manage"));
        tabs->addTab(new LazyPage([snap](QWidget *host) { return new HistoryPage(snap, host, QStringLiteral("snap")); }, tabs),
                     tr("History"));
        m_sourcesPage->addSource(tr("Snap"), QStringLiteral("widgets"), tabs);
    }
    return m_sourcesPage;
}

void MainWindow::buildShell()
{
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("WindowFrame"));
    central->setAttribute(Qt::WA_StyledBackground);
    m_windowFrame = central;
    setCentralWidget(central);

    m_rail = new NavigationRail(central);
    m_pages = new QStackedWidget(central);
    m_sheet = new DetailsSheet(m_hub->sourcesPtr(), m_flatpakBackend.get(), central);
    m_snackbar = new Snackbar(central);

    m_bottomBar = new NavigationRail(central, Qt::Horizontal);
    m_bottomBar->hide();
    auto addPage = [this](const QString &key, const QString &title, const QString &icon, QWidget *page) {
        m_pageKeys << key;
        m_pageTitles << title;
        m_pages->addWidget(page);
        m_rail->addDestination(icon, title);
        if (m_pageKeys.size() <= kBottomDestinations)
            m_bottomBar->addDestination(icon, title);
    };

    const int nativeSource = m_hub->nativeSource();
    auto *home = new HomePage(m_hub, m_backend.get(), nativeSource >= 0 ? m_hub->sources()[nativeSource].key : QString(), m_pages);
    m_home = home;
    m_discover = new DiscoverPage(m_hub, m_sheet, m_pages);
    auto *installed = new LibraryPage(m_hub, m_sheet, m_pages);
    auto *updates = new AllUpdatesPage(m_hub, m_sheet, m_pages);
    addPage(QStringLiteral("home"), tr("Home"), QStringLiteral("home"), home);
    addPage(QStringLiteral("discover"), tr("Discover"), QStringLiteral("explore"), m_discover);
    addPage(QStringLiteral("installed"), tr("Installed"), QStringLiteral("apps"), installed);
    addPage(QStringLiteral("updates"), tr("Updates"), QStringLiteral("update"), updates);
    // Groups: the native package manager's package groups (pacman groups,
    // dnf groups and environments, tasksel tasks). Settings → Layout can
    // hide it.
    if (m_backend) {
        PackageBackend *native = m_backend.get();
        auto *groups = new QWidget(m_pages);
        auto *groupsLayout = new QVBoxLayout(groups);
        groupsLayout->setContentsMargins(24, 4, 18, 12);
        groupsLayout->addWidget(lazy<GroupsPage>(native, groups));
        m_groupsIndex = m_pageKeys.size();
        addPage(QStringLiteral("groups"), tr("Groups"), QStringLiteral("category"), groups);
        applyDisableGroupView(AppSettings::instance().disableGroupView());
        connect(&AppSettings::instance(), &AppSettings::disableGroupViewChanged, this, &MainWindow::applyDisableGroupView);
    }
    if (m_flatpakBackend)
        addPage(QStringLiteral("flatpak"), tr("Flatpak"), QStringLiteral("deployed_code"), buildFlatpakPage());
    // Sources: what's particular to each source — its repositories or
    // remotes, mirrors, history, AUR build scripts, Snap management.
    addPage(QStringLiteral("sources"), tr("Sources"), QStringLiteral("hub"), buildSourceTabs());

    // Tools: housekeeping that isn't about packages (the former User
    // Management group).
    auto *tools = new QWidget(m_pages);
    auto *toolsLayout = new QVBoxLayout(tools);
    toolsLayout->setContentsMargins(24, 4, 18, 12);
    QTabWidget *toolTabs = sourceTabs(tools);
    toolTabs->addTab(buildFreeUpSpacePage(toolTabs), tr("Free up space"));
    toolTabs->addTab(buildBackupPage(toolTabs), tr("Backup && Restore"));
    toolTabs->addTab(new ExportImportPage(m_hub, nativeSource >= 0 ? m_hub->sources()[nativeSource].key : QString(), toolTabs),
                     tr("Export && Import"));
    m_snapshotsTab = new LazyPage([](QWidget *host) { return new SnapshotsPage(host); }, toolTabs);
    toolTabs->addTab(m_snapshotsTab, tr("Snapshots"));
    m_toolTabs = toolTabs;
    toolsLayout->addWidget(toolTabs);
    addPage(QStringLiteral("tools"), tr("Tools"), QStringLiteral("build"), tools);

    m_rail->addStretch();
    auto *settingsScroll = new QScrollArea(m_pages);
    settingsScroll->setProperty("m3", QStringLiteral("page"));
    settingsScroll->setFrameShape(QFrame::NoFrame);
    settingsScroll->setWidgetResizable(true);
    auto *settingsHost = new QWidget(settingsScroll);
    auto *settingsLayout = new QVBoxLayout(settingsHost);
    settingsLayout->setContentsMargins(24, 4, 24, 24);
    auto *settings = new SettingsPage(settingsHost);
    settings->setMaximumWidth(860);
    settingsLayout->addWidget(settings, 0, Qt::AlignTop | Qt::AlignLeft);
    settingsScroll->setWidget(settingsHost);
    addPage(QStringLiteral("settings"), tr("Settings"), QStringLiteral("settings"), settingsScroll);

    // Content pane: title bar over the current page.
    auto *pane = new QFrame(central);
    pane->setObjectName(QStringLiteral("ContentPane"));
    m_pageTitle = M3::label(QString(), QStringLiteral("title"), pane);
    auto *themeButton = new QPushButton(pane);
    MaterialTheme::styleButton(themeButton, QStringLiteral("icon"), QStringLiteral("contrast"));
    themeButton->setToolTip(tr("Switch between following the system, light, and dark"));
    connect(themeButton, &QPushButton::clicked, this, [this]() {
        const ThemeMode next = AppSettings::instance().themeMode() == ThemeMode::System ? ThemeMode::Light
            : AppSettings::instance().themeMode() == ThemeMode::Light                     ? ThemeMode::Dark
                                                                                          : ThemeMode::System;
        AppSettings::instance().setThemeMode(next);
        showMessage(next == ThemeMode::System ? tr("Following the system theme")
                    : next == ThemeMode::Light ? tr("Light theme")
                                               : tr("Dark theme"));
    });
    auto *menuButton = new QPushButton(pane);
    MaterialTheme::styleButton(menuButton, QStringLiteral("icon"), QStringLiteral("more_vert"));
    menuButton->setToolTip(tr("More"));
    m_overflowMenu = new QMenu(menuButton);
    connect(menuButton, &QPushButton::clicked, this, [this, menuButton]() {
        for (QAction *action : std::as_const(m_pageActions)) {
            m_overflowMenu->removeAction(action);
            action->deleteLater();
        }
        m_pageActions.clear();
        if (m_bottomBar->isVisible()) {
            for (int i = kBottomDestinations; i < m_pageKeys.size(); ++i) {
                if (i == m_groupsIndex && AppSettings::instance().disableGroupView())
                    continue;
                auto *action = new QAction(m_pageTitles[i], m_overflowMenu);
                const QString key = m_pageKeys[i];
                connect(action, &QAction::triggered, this, [this, key]() { goToPage(key); });
                m_overflowMenu->insertAction(m_pagesSeparator, action);
                m_pageActions.append(action);
            }
        }
        m_pagesSeparator->setVisible(!m_pageActions.isEmpty());
        m_overflowMenu->popup(
            menuButton->mapToGlobal(QPoint(menuButton->width() - m_overflowMenu->sizeHint().width(), menuButton->height())));
    });

    // Window controls, shown only with Athena's own title bar.
    m_windowControls = new QWidget(pane);
    auto *controlsLayout = new QHBoxLayout(m_windowControls);
    controlsLayout->setContentsMargins(8, 0, 0, 0);
    controlsLayout->setSpacing(2);
    auto *minimizeButton = new QPushButton(m_windowControls);
    MaterialTheme::styleButton(minimizeButton, QStringLiteral("window"), QStringLiteral("remove"));
    minimizeButton->setToolTip(tr("Minimize"));
    connect(minimizeButton, &QPushButton::clicked, this, &QWidget::showMinimized);
    m_maximizeButton = new QPushButton(m_windowControls);
    MaterialTheme::styleButton(m_maximizeButton, QStringLiteral("window"), QStringLiteral("crop_square"));
    m_maximizeButton->setToolTip(tr("Maximize"));
    connect(m_maximizeButton, &QPushButton::clicked, this, [this]() { m_frameless->toggleMaximized(); });
    auto *closeButton = new QPushButton(m_windowControls);
    MaterialTheme::styleButton(closeButton, QStringLiteral("windowClose"), QStringLiteral("close"));
    closeButton->setToolTip(tr("Close"));
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    for (QPushButton *button : {minimizeButton, m_maximizeButton, closeButton})
        controlsLayout->addWidget(button);

    m_titleBar = new QWidget(pane);
    auto *titleRow = new QHBoxLayout(m_titleBar);
    titleRow->setContentsMargins(24, 12, 12, 8);
    titleRow->addWidget(m_pageTitle, 1);
    titleRow->addWidget(themeButton);
    titleRow->addWidget(menuButton);
    titleRow->addWidget(m_windowControls);
    auto *paneLayout = new QVBoxLayout(pane);
    paneLayout->setContentsMargins(0, 0, 0, 0);
    paneLayout->setSpacing(0);
    paneLayout->addWidget(m_titleBar);
    paneLayout->addWidget(m_pages, 1);

    m_updateBanner = new UpdateBannerWidget(central);
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addWidget(m_rail);
    row->addWidget(pane, 1);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(m_updateBanner);
    centralLayout->addLayout(row, 1);
    centralLayout->addWidget(m_bottomBar);

    connect(m_rail, &NavigationRail::currentChanged, this, [this](int index) { goToPage(m_pageKeys.value(index)); });
    connect(m_bottomBar, &NavigationRail::currentChanged, this, [this](int index) { goToPage(m_pageKeys.value(index)); });
    connect(home, &HomePage::navigate, this, [this](const QString &key) {
        if (key == QLatin1String("history") && m_systemTabs) {
            // Home's "See all": the system source's full history.
            goToPage(QStringLiteral("sources"));
            m_sourcesPage->showSource(0);
            for (int i = 0; i < m_systemTabs->count(); ++i) {
                if (m_systemTabs->tabText(i) == tr("History"))
                    m_systemTabs->setCurrentIndex(i);
            }
            return;
        }
        if (key == QLatin1String("snapshots")) {
            goToPage(QStringLiteral("tools"));
            m_toolTabs->setCurrentWidget(m_snapshotsTab);
            return;
        }
        goToPage(key);
    });
    connect(home, &HomePage::updateEverything, this, &MainWindow::updateAll);
    connect(home, &HomePage::reviewAurUpdates, this, &MainWindow::reviewAurUpdates);
    connect(updates, &AllUpdatesPage::updateEverything, this, &MainWindow::updateAll);
    connect(m_sheet, &DetailsSheet::packagesChanged, m_hub, &CatalogHub::refreshAll);
    connect(m_sheet, &DetailsSheet::message, this, &MainWindow::showMessage);
    // The sheet's "Edit permissions": Flatpak → Permissions, on that app.
    connect(m_sheet, &DetailsSheet::editPermissions, this, [this](const QString &appId) {
        if (!m_flatpakSections)
            return;
        goToPage(QStringLiteral("flatpak"));
        m_flatpakSegments->button(0)->setChecked(true);
        Motion::switchStack(m_flatpakSections, 0);
        // The section builds its page on first show.
        QTimer::singleShot(0, this, [this, appId]() {
            if (auto *page = m_flatpakSections->widget(0)->findChild<PermissionsPage *>())
                page->selectApp(appId);
        });
    });
}

QWidget *MainWindow::buildFlatpakPage()
{
    // Flatpak's own tools — what a sandboxed app may reach, the data it
    // keeps, and what uninstalled apps left behind — as one page with a
    // segmented button per section. (Backing up app data is with the other
    // backups, under Tools.)
    FlatpakBackend *flatpak = m_flatpakBackend.get();
    auto *page = new QWidget(m_pages);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 4, 18, 12);
    layout->setSpacing(12);
    auto *switcher = new QHBoxLayout;
    m_flatpakSegments = M3::segmented({{tr("Permissions"), QStringLiteral("lock")},
                                       {tr("App data"), QStringLiteral("folder")},
                                       {tr("Leftovers"), QStringLiteral("delete_sweep")}},
                                      page, switcher);
    switcher->addStretch(1);
    layout->addLayout(switcher);

    m_flatpakSections = new QStackedWidget(page);
    m_flatpakSections->addWidget(lazy<PermissionsPage>(flatpak, m_flatpakSections));
    m_flatpakSections->addWidget(lazy<UserDataPage>(flatpak, m_flatpakSections));
    m_flatpakSections->addWidget(lazy<LeftoverDataPage>(flatpak, m_flatpakSections));
    layout->addWidget(m_flatpakSections, 1);
    m_flatpakSegments->button(0)->setChecked(true);
    connect(m_flatpakSegments, &QButtonGroup::idClicked, this, [this](int id) { Motion::switchStack(m_flatpakSections, id); });
    return page;
}

// A page of sections switched by a segmented button.
static QWidget *segmentedPage(QWidget *parent, const QVector<QPair<QString, QString>> &labels, const QVector<QWidget *> &pages,
                              QWidget *above = nullptr)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(12);
    if (above)
        layout->addWidget(above);
    auto *stack = new QStackedWidget(page);
    for (QWidget *section : pages)
        stack->addWidget(section);
    if (pages.size() > 1) {
        auto *switcher = new QHBoxLayout;
        QButtonGroup *group = M3::segmented(labels, page, switcher);
        switcher->addStretch(1);
        layout->addLayout(switcher);
        group->button(0)->setChecked(true);
        QObject::connect(group, &QButtonGroup::idClicked, stack, [stack](int id) { Motion::switchStack(stack, id); });
    }
    layout->addWidget(stack, 1);
    return page;
}

QWidget *MainWindow::buildFreeUpSpacePage(QWidget *parent)
{
    // Everything that frees disk space in one place: the package manager's
    // cache and unused dependencies (the same dialog as Home's Cleanup
    // tile), then your own files.
    QFrame *packages = nullptr;
    if (m_backend) {
        QVBoxLayout *content = nullptr;
        packages = M3::section(parent, QStringLiteral("cleaning_services"), tr("Packages"),
                               tr("Old package versions kept in the package cache, and dependencies nothing needs anymore."),
                               content);
        auto *button = new QPushButton(tr("Clean up packages…"), packages);
        MaterialTheme::styleButton(button, QStringLiteral("tonal"), QStringLiteral("cleaning_services"));
        content->addWidget(button, 0, Qt::AlignLeft);
        const int native = m_hub->nativeSource();
        const QString nativeKey = native >= 0 ? m_hub->sources()[native].key : QString();
        connect(button, &QPushButton::clicked, this, [this, nativeKey]() {
            CleanupDialog dialog(nativeKey, m_backend.get(), this);
            if (dialog.runModal())
                m_hub->refreshInstalled();
        });
    }
    auto *everyday = new UserCleanupPage(tr("Temporary files and caches that are always safe to clear; they're recreated "
                                            "as needed."),
                                         UserCleanupTargets::general(), parent);
    auto *more = new UserCleanupPage(tr("Still your own data, but worth a second look before clearing: history, the "
                                        "recently-used list, the trash."),
                                     UserCleanupTargets::advanced(), parent);
    return segmentedPage(parent, {{tr("Everyday"), QStringLiteral("cached")}, {tr("Worth a second look"), QStringLiteral("history")}},
                         {everyday, more}, packages);
}

QWidget *MainWindow::buildBackupPage(QWidget *parent)
{
    // Every backup in one place: your settings and dotfiles, and (with
    // Flatpak) apps' data. Lists of installed apps are Export & Import.
    QVector<QWidget *> pages = {new UserBackupRestorePage(parent)};
    if (m_flatpakBackend)
        pages.append(lazy<BackupRestorePage>(m_flatpakBackend.get(), parent));
    return segmentedPage(parent, {{tr("Your files"), QStringLiteral("folder")}, {tr("Flatpak apps"), QStringLiteral("deployed_code")}},
                         pages);
}

void MainWindow::setupActions()
{
    // No menu bar in the new layout: the window-wide shortcuts live on the
    // window itself, and the rest is in the title bar's overflow menu.
    auto *reload = new QAction(tr("Reload Package Information"), this);
    reload->setShortcut(QKeySequence(tr("Ctrl+R")));
    connect(reload, &QAction::triggered, this, &MainWindow::reloadAllPackageInformation);
    auto *find = new QAction(tr("Find Package"), this);
    find->setShortcut(QKeySequence::Find);
    connect(find, &QAction::triggered, this, [this]() {
        goToPage(QStringLiteral("discover"));
        m_discover->focusSearch();
    });
    auto *checkUpdates = new QAction(tr("Check for Athena Updates"), this);
    connect(checkUpdates, &QAction::triggered, this, &MainWindow::checkForApplicationUpdates);
    auto *reportBug = new QAction(tr("Report a Bug…"), this);
    connect(reportBug, &QAction::triggered, this, []() { QDesktopServices::openUrl(QUrl(QString::fromLatin1(kIssuesUrl))); });
    auto *about = new QAction(tr("About Athena"), this);
    connect(about, &QAction::triggered, this, &MainWindow::showAboutDialog);
    auto *quit = new QAction(tr("Quit"), this);
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);

    for (QAction *action : {reload, find, quit})
        addAction(action);

    // In narrow windows the bottom bar only fits the first pages; the rest
    // are listed at the top of this menu then (and only then — with the
    // rail they're one click away already).
    m_pagesSeparator = m_overflowMenu->addSeparator();
    m_overflowMenu->addAction(reload);
    m_overflowMenu->addSeparator();
    m_overflowMenu->addAction(checkUpdates);
    m_overflowMenu->addAction(reportBug);
    m_overflowMenu->addAction(about);
    m_overflowMenu->addSeparator();
    m_overflowMenu->addAction(quit);
}

void MainWindow::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    m_trayIcon = new QSystemTrayIcon(MaterialTheme::icon(QStringLiteral("system_update_alt"), MaterialTheme::current().primary, 64),
                                     this);
    m_trayIcon->setToolTip(tr("Athena"));
    auto *trayMenu = new QMenu(this);
    connect(trayMenu->addAction(tr("Settings")), &QAction::triggered, this, [this]() {
        show();
        raise();
        goToPage(QStringLiteral("settings"));
    });
    m_trayUpdateAction = trayMenu->addAction(tr("Update"));
    m_trayUpdateAction->setEnabled(false);
    connect(m_trayUpdateAction, &QAction::triggered, this, &MainWindow::updateAll);
    trayMenu->addSeparator();
    connect(trayMenu->addAction(tr("Quit")), &QAction::triggered, qApp, &QApplication::quit);
    m_trayIcon->setContextMenu(trayMenu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason != QSystemTrayIcon::Trigger)
            return;
        setVisible(!isVisible());
        if (isVisible()) {
            raise();
            activateWindow();
        }
    });
    m_trayIcon->setVisible(!AppSettings::instance().hideTrayWhenNoUpdates());

    m_updateCheckTimer = new QTimer(this);
    connect(m_updateCheckTimer, &QTimer::timeout, m_hub, &CatalogHub::refreshUpdates);
    auto applyInterval = [this](int minutes) { m_updateCheckTimer->start(minutes * 60000); };
    applyInterval(AppSettings::instance().updateCheckIntervalMinutes());
    connect(&AppSettings::instance(), &AppSettings::updateCheckIntervalMinutesChanged, this, applyInterval);
}

void MainWindow::goToPage(const QString &key)
{
    const int index = m_pageKeys.indexOf(key);
    if (index < 0)
        return;
    m_sheet->close();
    Motion::switchStack(m_pages, index);
    m_pageTitle->setText(m_pageTitles[index]);
    if (index < kBottomDestinations) {
        if (m_bottomBar->currentIndex() != index)
            m_bottomBar->setCurrentIndex(index);
    } else {
        m_bottomBar->clearSelection();
    }
    if (m_rail->currentIndex() != index)
        m_rail->setCurrentIndex(index);
}

void MainWindow::applyTitleBarMode(bool custom)
{
    m_frameless->setEnabled(custom);
    m_windowControls->setVisible(custom);
    updateWindowFrame();
}

void MainWindow::updateWindowFrame()
{
    const bool framed = m_frameless && m_frameless->isEnabled() && !isMaximized() && !isFullScreen();
    if (m_windowFrame->property("framed").toBool() != framed) {
        m_windowFrame->setProperty("framed", framed);
        m_windowFrame->layout()->setContentsMargins(framed ? 1 : 0, framed ? 1 : 0, framed ? 1 : 0, framed ? 1 : 0);
        m_windowFrame->style()->unpolish(m_windowFrame);
        m_windowFrame->style()->polish(m_windowFrame);
    }
    if (m_maximizeButton) {
        MaterialTheme::setButtonIcon(m_maximizeButton, isMaximized() ? QStringLiteral("filter_none") : QStringLiteral("crop_square"));
        m_maximizeButton->setToolTip(isMaximized() ? tr("Restore") : tr("Maximize"));
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    const bool narrow = width() < 720;
    if (m_rail && m_rail->isVisible() == narrow) {
        m_rail->setVisible(!narrow);
        m_bottomBar->setVisible(narrow);
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange)
        updateWindowFrame();
    QMainWindow::changeEvent(event);
}

void MainWindow::showMessage(const QString &text)
{
    m_snackbar->showMessage(text);
}

QVector<PackageBackend *> MainWindow::allBackends() const
{
    QVector<PackageBackend *> backends;
    for (const CatalogSource &source : m_hub->sources())
        backends.append(source.backend);
    return backends;
}

void MainWindow::onUpdatesChanged()
{
    m_pendingUpdatesByBackend.clear();
    for (const CatalogItem &item : m_hub->updates()) {
        const CatalogVariant &variant = item.variants.first();
        m_pendingUpdatesByBackend[m_hub->sources()[variant.source].backend].append(variant.info.name);
    }
    const int total = m_hub->updates().size();
    m_rail->setBadge(m_pageKeys.indexOf(QStringLiteral("updates")), total);
    m_bottomBar->setBadge(m_pageKeys.indexOf(QStringLiteral("updates")), total);
    updateTrayIconState(total);
}

void MainWindow::updateTrayIconState(int totalUpdates)
{
    if (!m_trayIcon)
        return;
    m_trayIcon->setVisible(totalUpdates > 0 || !AppSettings::instance().hideTrayWhenNoUpdates());
    m_trayIcon->setToolTip(totalUpdates > 0 ? tr("%n update(s) available", "", totalUpdates) : tr("Athena — up to date"));
    if (m_trayUpdateAction)
        m_trayUpdateAction->setEnabled(totalUpdates > 0);
}

void MainWindow::reviewAurUpdates()
{
    if (!m_aurBackend)
        return;
    const QStringList names = m_pendingUpdatesByBackend.value(m_aurBackend.get());
    if (names.isEmpty())
        return;
    PackageActions::upgradePackages(this, m_aurBackend.get(), names, [this](bool success) {
        showMessage(success ? tr("AUR updates installed.") : tr("AUR updates were not installed."));
        m_hub->refreshAll();
    });
}

void MainWindow::updateAll()
{
    if (m_pendingUpdatesByBackend.isEmpty())
        return;

    // Arch news since the last system upgrade comes first: some posts say
    // the upgrade needs a manual step.
    if (m_home && m_backend && m_pendingUpdatesByBackend.contains(m_backend.get())) {
        const QVector<SystemTools::NewsItem> unread = m_home->unreadNews();
        if (!unread.isEmpty()) {
            const bool intervention = std::any_of(unread.begin(), unread.end(), [](const SystemTools::NewsItem &n) { return n.manualIntervention; });
            QStringList titles;
            for (const SystemTools::NewsItem &item : unread)
                titles << QStringLiteral("• ") + item.title.toHtmlEscaped();
            M3Dialog dialog(this);
            dialog.setHeadline(intervention ? tr("Read this before updating") : tr("Arch news since your last update"),
                               intervention ? QStringLiteral("warning") : QStringLiteral("campaign"));
            dialog.setSupportingText((intervention ? tr("Arch Linux posted a notice that needs manual action before or after "
                                                        "upgrading:<br><br>%1")
                                                   : tr("%1"))
                                         .arg(titles.join(QStringLiteral("<br>"))));
            enum { Cancel, Read, Proceed };
            int choice = Cancel;
            QPushButton *cancel = dialog.addAction(tr("Cancel"), QStringLiteral("text"));
            QPushButton *read = dialog.addAction(tr("Read first"), intervention ? QStringLiteral("filled") : QStringLiteral("tonal"));
            QPushButton *proceed = dialog.addAction(tr("Update anyway"), intervention ? QStringLiteral("text") : QStringLiteral("filled"));
            connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
            connect(read, &QPushButton::clicked, &dialog, [&]() { choice = Read; dialog.accept(); });
            connect(proceed, &QPushButton::clicked, &dialog, [&]() { choice = Proceed; dialog.accept(); });
            dialog.exec();
            if (choice == Read) {
                for (const SystemTools::NewsItem &item : unread) {
                    NewsDialog news(item, this);
                    news.exec();
                }
                m_home->markNewsRead();
                return;
            }
            if (choice != Proceed)
                return;
        }
    }

    QMap<PackageBackend *, QStringList> namesByBackend = m_pendingUpdatesByBackend;
    // AUR updates are new builds from new build files, so (with review on)
    // they're pulled out of the unattended batch and go through the review
    // dialog once everything else is done.
    QStringList aurNames;
    if (m_aurBackend && AurBackend::reviewRequired() && namesByBackend.contains(m_aurBackend.get()))
        aurNames = namesByBackend.take(m_aurBackend.get());
    int total = 0;
    for (const QStringList &names : namesByBackend)
        total += names.size();

    auto reviewAur = [this, aurNames]() {
        if (aurNames.isEmpty()) {
            m_hub->refreshAll();
            return;
        }
        AurReviewDialog::reviewAndBuild(this, aurNames, true, [this](bool success) {
            showMessage(success ? tr("AUR updates installed.") : tr("AUR updates were not installed."));
            m_hub->refreshAll();
        });
    };
    if (namesByBackend.isEmpty()) {
        reviewAur();
        return;
    }

    const QString summary = tr("Install %1 available update(s) across all package managers?").arg(total)
        + (aurNames.isEmpty() ? QString()
                              : tr("\n\n%n AUR update(s) will open for PKGBUILD review afterwards.", "", aurNames.size()));
    // One snapshot for the whole batch, if any of it touches the system.
    QVector<ProcessRunner::Command> snapshot;
    for (PackageBackend *backend : namesByBackend.keys()) {
        snapshot = PackageActions::snapshotSteps(backend, QStringLiteral("updating everything"));
        if (!snapshot.isEmpty())
            break;
    }
    if (!TransactionConfirmDialog::confirm(this, tr("Update everything"), summary))
        return;
    // One terminal window for the whole batch: the snapshot, then each
    // source's own full update (pacman -Syu, dnf upgrade, flatpak update,
    // snap refresh) — or the named builds, for AUR without review.
    QVector<ProcessRunner::Command> commands = snapshot;
    for (auto it = namesByBackend.constBegin(); it != namesByBackend.constEnd(); ++it) {
        const bool aur = m_aurBackend && it.key() == m_aurBackend.get();
        commands += it.key()->upgradeCommands(aur ? it.value() : QStringList());
    }
    const bool success = PackageActions::runInTerminal(this, tr("Update everything"), commands);
    showMessage(success ? tr("Updates installed.") : tr("Some updates failed to install."));
    reviewAur();
}

void MainWindow::applyDisableGroupView(bool disabled)
{
    if (m_groupsIndex < 0)
        return;
    m_rail->setDestinationVisible(m_groupsIndex, !disabled);
    if (disabled && m_pages->currentIndex() == m_groupsIndex)
        goToPage(QStringLiteral("home"));
}

void MainWindow::reloadAllPackageInformation()
{
    const QVector<PackageBackend *> backends = allBackends();
    if (backends.isEmpty())
        return;
    PackageActions::confirmAndRun(
        this, tr("Reload Package Information"),
        tr("Refresh package metadata for all available package managers now?\n\nThis may require administrator "
           "privileges and can take a moment."),
        [backends]() -> OperationResult {
            OperationResult combined;
            combined.success = true;
            for (PackageBackend *backend : backends) {
                const OperationResult result = backend->refreshMetadata();
                combined.success = combined.success && result.success;
                if (!result.output.isEmpty())
                    combined.output += result.output + '\n';
            }
            return combined;
        },
        [this](bool success) {
            showMessage(success ? tr("Package information reloaded.") : tr("Failed to reload package information."));
            if (success) {
                m_hub->refreshAll();
                m_hub->appIndex()->load(); // pick up refreshed AppStream catalogs
            }
        });
}

void MainWindow::checkForApplicationUpdates()
{
    auto *checker = new GitHubReleaseChecker(this);
    connect(checker, &GitHubReleaseChecker::updateAvailable, this, [this, checker](const QString &version, const QString &htmlUrl) {
        m_updateBanner->showUpdate(version, htmlUrl);
        checker->deleteLater();
    });
    connect(checker, &GitHubReleaseChecker::upToDate, this, [this, checker]() {
        showMessage(tr("Athena is up to date."));
        checker->deleteLater();
    });
    connect(checker, &GitHubReleaseChecker::checkFailed, this, [this, checker](const QString &reason) {
        M3Message::warning(this, tr("Check for Updates"), tr("Couldn't check for updates.\n\n%1").arg(reason));
        checker->deleteLater();
    });
    checker->checkForUpdate();
}

void MainWindow::showAboutDialog()
{
    M3Message::about(this, tr("About Athena"),
                       tr("<h3>Athena %1</h3>"
                          "<p>A cross-distro package manager for DNF, APT, Pacman (plus the AUR), Flatpak, and Snap.</p>"
                          "<p><a href=\"%2\">%2</a></p>"
                          "<p>Icons: Material Symbols, © Google, Apache License 2.0.</p>")
                           .arg(QStringLiteral(ATHENA_VERSION), QString::fromLatin1(kProjectUrl)));
}
