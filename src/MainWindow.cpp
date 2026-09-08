#include "MainWindow.h"

#include <QAction>
#include <QComboBox>
#include <QDesktopServices>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QScreen>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStringList>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "core/AppSettings.h"
#include "core/BackendFactory.h"
#include "core/GitHubReleaseChecker.h"
#include "core/Version.h"
#include "core/backends/FlatpakBackend.h"
#include "core/backends/SnapBackend.h"
#include "models/PackageTableModel.h"
#include "ui/AppIcons.h"
#include "ui/BackupRestorePage.h"
#include "ui/FirstRunDialog.h"
#include "ui/GroupsPage.h"
#include "ui/HistoryPage.h"
#include "ui/InstalledPage.h"
#include "ui/LeftoverDataPage.h"
#include "ui/PackageActions.h"
#include "ui/PackageBrowser.h"
#include "ui/PermissionsPage.h"
#include "ui/PreferencesDialog.h"
#include "ui/RepositoriesPage.h"
#include "ui/SearchPage.h"
#include "ui/UpdateBannerWidget.h"
#include "ui/UpdatesPage.h"
#include "ui/UserBackupRestorePage.h"
#include "ui/UserCleanupPage.h"
#include "ui/UserDataPage.h"

namespace {
const char *kProjectUrl = "https://github.com/niruxx/Athena";
const char *kIssuesUrl = "https://github.com/niruxx/Athena/issues";

// backendName() returns a descriptive label like "DNF (Fedora / RHEL)";
// the group dropdown just wants the bare tool name ("dnf") to fit
// "Repository (dnf)".
QString shortBackendName(const QString &backendName)
{
    const int parenIndex = backendName.indexOf(" (");
    return (parenIndex >= 0 ? backendName.left(parenIndex) : backendName).toLower();
}
} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("Athena"));
    resize(1150, 750);

    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        move(available.x() + (available.width() - width()) / 2,
             available.y() + (available.height() - height()) / 2);
    }

    m_backend = BackendFactory::createForHostSystem();

    m_groupStack = new QStackedWidget(this);
    m_groupCombo = new QComboBox(this);
    int systemGroupIndex = -1;
    int flatpakGroupIndex = -1;
    int snapGroupIndex = -1;

    // Every PackageBrowser's counts feed the bottom-bar stats summary, so
    // whichever page/group is currently visible keeps it live as data
    // loads or checkboxes are toggled.
    auto watchBrowserStats = [this](PackageBrowser *browser) {
        connect(browser->model(), &QAbstractItemModel::modelReset, this, &MainWindow::updateStatusBarStats);
        connect(browser->model(), &PackageTableModel::checkedChanged, this, &MainWindow::updateStatusBarStats);
    };

    // m_groupCombo's entries are added in the same order as m_groupStack's
    // pages, so a combo index always matches the stack page it should show.
    if (m_backend) {
        m_systemTabs = new QTabWidget(m_groupStack);
        auto *installedPage = new InstalledPage(m_backend.get(), m_systemTabs);
        auto *updatesPage = new UpdatesPage(m_backend.get(), m_systemTabs);
        auto *searchPage = new SearchPage(m_backend.get(), m_systemTabs);
        m_systemGroupsPage = new GroupsPage(m_backend.get(), m_systemTabs);
        m_systemTabs->addTab(installedPage, tr("Installed"));
        m_systemTabs->addTab(updatesPage, tr("Updates"));
        m_systemTabs->addTab(searchPage, tr("Search"));
        if (!AppSettings::instance().disableGroupView())
            m_systemTabs->addTab(m_systemGroupsPage, tr("Groups"));
        m_systemTabs->addTab(
            new RepositoriesPage({{m_backend.get(), tr("System")}}, m_systemTabs), tr("Repositories"));
        m_systemTabs->addTab(new HistoryPage(m_backend.get(), m_systemTabs), tr("History"));
        systemGroupIndex = m_groupStack->addWidget(m_systemTabs);
        m_groupCombo->addItem(tr("Repository (%1)").arg(shortBackendName(m_backend->backendName())));
        statusBar()->showMessage(tr("Backend: %1").arg(m_backend->backendName()));

        watchBrowserStats(installedPage->browser());
        watchBrowserStats(updatesPage->browser());
        watchBrowserStats(searchPage->browser());
        watchBrowserStats(m_systemGroupsPage->browser());
        connect(m_systemTabs, &QTabWidget::currentChanged, this, &MainWindow::updateStatusBarStats);
        connect(&AppSettings::instance(), &AppSettings::disableGroupViewChanged, this,
                &MainWindow::applyDisableGroupView);
    } else {
        // Wrapped in a QTabWidget (with a single tab) rather than added to
        // the stack directly, so this page has a tab bar to host
        // m_groupCombo's corner widget too, same as every other group.
        m_systemTabs = new QTabWidget(m_groupStack);
        auto *label = new QLabel(
            tr("No supported package manager (dnf, apt, or pacman) was found on this system."), m_systemTabs);
        label->setAlignment(Qt::AlignCenter);
        label->setWordWrap(true);
        m_systemTabs->addTab(label, tr("System"));
        systemGroupIndex = m_groupStack->addWidget(m_systemTabs);
        m_groupCombo->addItem(tr("Repository"));
    }

    auto flatpakBackend = std::make_unique<FlatpakBackend>();
    if (flatpakBackend->isAvailable()) {
        m_flatpakBackend = std::move(flatpakBackend);

        m_flatpakTabs = new QTabWidget(m_groupStack);
        auto *installedPage = new InstalledPage(m_flatpakBackend.get(), m_flatpakTabs);
        auto *updatesPage = new UpdatesPage(m_flatpakBackend.get(), m_flatpakTabs);
        auto *searchPage = new SearchPage(m_flatpakBackend.get(), m_flatpakTabs);
        m_flatpakTabs->addTab(installedPage, tr("Installed"));
        m_flatpakTabs->addTab(updatesPage, tr("Updates"));
        m_flatpakTabs->addTab(searchPage, tr("Search"));
        m_flatpakTabs->addTab(
            new RepositoriesPage({{m_flatpakBackend.get(), tr("Flatpak")}}, m_flatpakTabs), tr("Repositories"));
        m_flatpakTabs->addTab(new PermissionsPage(m_flatpakBackend.get(), m_flatpakTabs), tr("Permissions"));
        m_flatpakTabs->addTab(new UserDataPage(m_flatpakBackend.get(), m_flatpakTabs), tr("User Data"));
        m_flatpakTabs->addTab(new LeftoverDataPage(m_flatpakBackend.get(), m_flatpakTabs), tr("Leftover Data"));
        m_flatpakTabs->addTab(new BackupRestorePage(m_flatpakBackend.get(), m_flatpakTabs), tr("Backup & Restore"));
        m_flatpakTabs->addTab(new HistoryPage(m_flatpakBackend.get(), m_flatpakTabs), tr("History"));
        flatpakGroupIndex = m_groupStack->addWidget(m_flatpakTabs);
        m_groupCombo->addItem(tr("Flatpak"));

        watchBrowserStats(installedPage->browser());
        watchBrowserStats(updatesPage->browser());
        watchBrowserStats(searchPage->browser());
        connect(m_flatpakTabs, &QTabWidget::currentChanged, this, &MainWindow::updateStatusBarStats);
    }

    auto snapBackend = std::make_unique<SnapBackend>();
    if (snapBackend->isAvailable()) {
        m_snapBackend = std::move(snapBackend);

        m_snapTabs = new QTabWidget(m_groupStack);
        auto *installedPage = new InstalledPage(m_snapBackend.get(), m_snapTabs);
        auto *updatesPage = new UpdatesPage(m_snapBackend.get(), m_snapTabs);
        auto *searchPage = new SearchPage(m_snapBackend.get(), m_snapTabs);
        m_snapTabs->addTab(installedPage, tr("Installed"));
        m_snapTabs->addTab(updatesPage, tr("Updates"));
        m_snapTabs->addTab(searchPage, tr("Search"));
        m_snapTabs->addTab(new HistoryPage(m_snapBackend.get(), m_snapTabs), tr("History"));
        snapGroupIndex = m_groupStack->addWidget(m_snapTabs);
        m_groupCombo->addItem(tr("Snap"));

        watchBrowserStats(installedPage->browser());
        watchBrowserStats(updatesPage->browser());
        watchBrowserStats(searchPage->browser());
        connect(m_snapTabs, &QTabWidget::currentChanged, this, &MainWindow::updateStatusBarStats);
    }

    m_userMgmtTabs = new QTabWidget(m_groupStack);
    m_userMgmtTabs->addTab(new UserCleanupPage(UserCleanupTargets::general(), m_userMgmtTabs), tr("General"));
    m_userMgmtTabs->addTab(new UserCleanupPage(UserCleanupTargets::advanced(), m_userMgmtTabs), tr("Advanced"));
    m_userMgmtTabs->addTab(new UserBackupRestorePage(m_userMgmtTabs), tr("Backup & Restore"));
    m_groupStack->addWidget(m_userMgmtTabs);
    m_groupCombo->addItem(tr("User Management"));

    connect(m_groupCombo, &QComboBox::currentIndexChanged, m_groupStack, &QStackedWidget::setCurrentIndex);

    setupMenuBar();

    m_statsLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_statsLabel);
    connect(m_groupStack, &QStackedWidget::currentChanged, this, &MainWindow::updateStatusBarStats);
    updateStatusBarStats();

    // Every group page is a QTabWidget, so rather than giving m_groupCombo
    // its own row above them, it rides in the tab bar's corner — sharing
    // that row instead of adding a second one. Since it's one shared combo
    // (not one per group), it has to move to whichever group's tab bar is
    // currently visible.
    connect(m_groupStack, &QStackedWidget::currentChanged, this, &MainWindow::placeGroupComboInCornerWidget);
    placeGroupComboInCornerWidget();

    m_updateBanner = new UpdateBannerWidget(this);

    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(m_updateBanner);
    centralLayout->addWidget(m_groupStack, 1);
    setCentralWidget(central);

    const StartupTab requestedStartupTab = AppSettings::instance().startupTab();
    int startupIndex = systemGroupIndex;
    if (requestedStartupTab == StartupTab::Flatpak && flatpakGroupIndex >= 0)
        startupIndex = flatpakGroupIndex;
    else if (requestedStartupTab == StartupTab::Snap && snapGroupIndex >= 0)
        startupIndex = snapGroupIndex;
    if (startupIndex >= 0)
        m_groupCombo->setCurrentIndex(startupIndex);

    if (AppSettings::instance().checkForAppUpdatesOnStartup()) {
        auto *checker = new GitHubReleaseChecker(this);
        connect(checker, &GitHubReleaseChecker::updateAvailable, this,
                [this](const QString &version, const QString &htmlUrl) {
                    m_updateBanner->showUpdate(version, htmlUrl);
                });
        checker->checkForUpdate();
    }

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_trayIcon = new QSystemTrayIcon(AppIcons::update(), this);
        m_trayIcon->setToolTip(tr("Athena"));

        auto *trayMenu = new QMenu(this);
        QAction *traySettingsAction = trayMenu->addAction(tr("Settings"));
        connect(traySettingsAction, &QAction::triggered, this, [this]() {
            PreferencesDialog dialog(this);
            dialog.exec();
        });
        m_trayUpdateAction = trayMenu->addAction(tr("Update"));
        m_trayUpdateAction->setEnabled(false);
        connect(m_trayUpdateAction, &QAction::triggered, this, &MainWindow::updateAllFromTray);
        trayMenu->addSeparator();
        QAction *trayQuitAction = trayMenu->addAction(tr("Quit"));
        connect(trayQuitAction, &QAction::triggered, qApp, &QApplication::quit);
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
        connect(m_updateCheckTimer, &QTimer::timeout, this, &MainWindow::pollForTrayUpdates);
        auto applyUpdateInterval = [this](int minutes) { m_updateCheckTimer->start(minutes * 60000); };
        applyUpdateInterval(AppSettings::instance().updateCheckIntervalMinutes());
        connect(&AppSettings::instance(), &AppSettings::updateCheckIntervalMinutesChanged, this, applyUpdateInterval);

        pollForTrayUpdates();
    }

    if (!AppSettings::instance().hasCompletedFirstRun()) {
        const QString backendName = m_backend ? m_backend->backendName() : QString();
        // Deferred so the main window is already visible behind it, rather
        // than the dialog appearing to launch before anything else does.
        QTimer::singleShot(0, this, [this, backendName]() {
            FirstRunDialog dialog(backendName, this);
            dialog.exec();
        });
    }
}

void MainWindow::placeGroupComboInCornerWidget()
{
    auto *tabs = qobject_cast<QTabWidget *>(m_groupStack->currentWidget());
    if (!tabs || tabs == m_groupComboHost)
        return;

    // Detach from the previous host first: QTabWidget::setCornerWidget()
    // no-ops if the widget passed in is already what it thinks its corner
    // widget is, so a tab widget visited earlier (its own corner already
    // recorded as m_groupCombo) would otherwise refuse to take it back.
    if (m_groupComboHost)
        m_groupComboHost->setCornerWidget(nullptr, Qt::TopRightCorner);

    tabs->setCornerWidget(m_groupCombo, Qt::TopRightCorner);
    m_groupCombo->show();
    m_groupComboHost = tabs;
}

void MainWindow::pollForTrayUpdates()
{
    QVector<PackageBackend *> backends;
    if (m_backend)
        backends.append(m_backend.get());
    if (m_flatpakBackend)
        backends.append(m_flatpakBackend.get());
    if (m_snapBackend)
        backends.append(m_snapBackend.get());
    if (backends.isEmpty())
        return;

    using BackendUpdates = QVector<std::pair<PackageBackend *, QVector<PackageInfo>>>;
    auto *watcher = new QFutureWatcher<BackendUpdates>(this);
    QFuture<BackendUpdates> future = QtConcurrent::run([backends]() {
        BackendUpdates results;
        for (PackageBackend *backend : backends)
            results.append({backend, backend->listUpdates()});
        return results;
    });

    connect(watcher, &QFutureWatcher<BackendUpdates>::finished, this, [this, watcher]() {
        const BackendUpdates results = watcher->result();
        watcher->deleteLater();

        m_pendingUpdatesByBackend.clear();
        int total = 0;
        for (const auto &entry : results) {
            if (!entry.second.isEmpty())
                m_pendingUpdatesByBackend.insert(entry.first, entry.second);
            total += entry.second.size();
        }
        updateTrayIconState(total);
    });
    watcher->setFuture(future);
}

void MainWindow::updateTrayIconState(int totalUpdates)
{
    if (!m_trayIcon)
        return;

    const bool shouldShow = totalUpdates > 0 || !AppSettings::instance().hideTrayWhenNoUpdates();
    m_trayIcon->setVisible(shouldShow);
    m_trayIcon->setToolTip(totalUpdates > 0 ? tr("%n update(s) available", "", totalUpdates)
                                             : tr("Athena — up to date"));
    if (m_trayUpdateAction)
        m_trayUpdateAction->setEnabled(totalUpdates > 0);
}

void MainWindow::updateAllFromTray()
{
    if (m_pendingUpdatesByBackend.isEmpty())
        return;

    QMap<PackageBackend *, QStringList> namesByBackend;
    int total = 0;
    for (auto it = m_pendingUpdatesByBackend.constBegin(); it != m_pendingUpdatesByBackend.constEnd(); ++it) {
        QStringList names;
        for (const PackageInfo &pkg : it.value())
            names << pkg.name;
        namesByBackend.insert(it.key(), names);
        total += names.size();
    }

    const QString summary = tr("Install %1 available update(s) across all package managers?").arg(total);
    PackageActions::confirmAndRun(
        this, tr("Update All"), summary,
        [namesByBackend]() -> OperationResult {
            OperationResult combined;
            combined.success = true;
            for (auto it = namesByBackend.constBegin(); it != namesByBackend.constEnd(); ++it) {
                const OperationResult result = it.key()->upgradePackages(it.value());
                combined.success = combined.success && result.success;
                if (!result.output.isEmpty())
                    combined.output += result.output + '\n';
            }
            return combined;
        },
        [this](bool success) {
            statusBar()->showMessage(success ? tr("Updates installed.") : tr("Some updates failed to install."),
                                      5000);
            pollForTrayUpdates();
        });
}

void MainWindow::applyDisableGroupView(bool disabled)
{
    if (!m_systemTabs || !m_systemGroupsPage)
        return;

    const int existingIndex = m_systemTabs->indexOf(m_systemGroupsPage);
    if (disabled) {
        if (existingIndex >= 0)
            m_systemTabs->removeTab(existingIndex);
        return;
    }

    if (existingIndex >= 0)
        return;

    // Re-insert right after "Search" (or at the end if that tab is somehow
    // gone), keeping the same Installed/Updates/Search/Groups/... order it
    // started in.
    int insertIndex = m_systemTabs->count();
    for (int i = 0; i < m_systemTabs->count(); ++i) {
        if (m_systemTabs->tabText(i) == tr("Search")) {
            insertIndex = i + 1;
            break;
        }
    }
    m_systemTabs->insertTab(insertIndex, m_systemGroupsPage, tr("Groups"));
}

QTabWidget *MainWindow::currentGroupTabs() const
{
    if (!m_groupStack)
        return nullptr;

    QWidget *current = m_groupStack->currentWidget();
    if (current == m_systemTabs)
        return m_systemTabs;
    if (current == m_flatpakTabs)
        return m_flatpakTabs;
    if (current == m_snapTabs)
        return m_snapTabs;
    return nullptr;
}

PackageBackend *MainWindow::currentGroupBackend() const
{
    if (!m_groupStack)
        return nullptr;

    QWidget *current = m_groupStack->currentWidget();
    if (current == m_systemTabs)
        return m_backend.get();
    if (current == m_flatpakTabs)
        return m_flatpakBackend.get();
    if (current == m_snapTabs)
        return m_snapBackend.get();
    return nullptr;
}

void MainWindow::focusGroupTab(const QString &tabText)
{
    QTabWidget *group = currentGroupTabs();
    if (!group)
        return;

    for (int i = 0; i < group->count(); ++i) {
        if (group->tabText(i) == tabText) {
            group->setCurrentIndex(i);
            return;
        }
    }
}

PackageBrowser *MainWindow::currentPackageBrowser() const
{
    QTabWidget *group = currentGroupTabs();
    if (!group)
        return nullptr;

    QWidget *current = group->currentWidget();
    if (auto *page = qobject_cast<InstalledPage *>(current))
        return page->browser();
    if (auto *page = qobject_cast<UpdatesPage *>(current))
        return page->browser();
    if (auto *page = qobject_cast<SearchPage *>(current))
        return page->browser();
    if (auto *page = qobject_cast<GroupsPage *>(current))
        return page->browser();
    return nullptr;
}

void MainWindow::updateStatusBarStats()
{
    if (!m_statsLabel)
        return;

    PackageBrowser *browser = currentPackageBrowser();
    if (!browser) {
        m_statsLabel->clear();
        return;
    }

    const PackageBrowser::Stats stats = browser->stats();
    m_statsLabel->setText(tr("%1 listed, %2 installed, %3 to install/upgrade, %4 to remove")
                               .arg(stats.listed)
                               .arg(stats.installed)
                               .arg(stats.toInstallOrUpgrade)
                               .arg(stats.toRemove));
}

void MainWindow::reloadAllPackageInformation()
{
    QVector<PackageBackend *> backends;
    if (m_backend)
        backends.append(m_backend.get());
    if (m_flatpakBackend)
        backends.append(m_flatpakBackend.get());
    if (m_snapBackend)
        backends.append(m_snapBackend.get());
    if (backends.isEmpty())
        return;

    PackageActions::confirmAndRun(
        this, tr("Reload Package Information"),
        tr("Refresh package metadata for all available package managers now?\n\nThis may "
           "require administrator privileges and can take a moment."),
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
            statusBar()->showMessage(success ? tr("Package information reloaded.")
                                              : tr("Failed to reload package information."),
                                      5000);
        });
}

void MainWindow::checkForApplicationUpdates()
{
    auto *checker = new GitHubReleaseChecker(this);
    connect(checker, &GitHubReleaseChecker::updateAvailable, this,
            [this, checker](const QString &version, const QString &htmlUrl) {
                m_updateBanner->showUpdate(version, htmlUrl);
                checker->deleteLater();
            });
    connect(checker, &GitHubReleaseChecker::upToDate, this, [this, checker]() {
        QMessageBox::information(this, tr("Check for Updates"), tr("Athena is up to date."));
        checker->deleteLater();
    });
    connect(checker, &GitHubReleaseChecker::checkFailed, this, [this, checker](const QString &reason) {
        QMessageBox::warning(this, tr("Check for Updates"),
                              tr("Couldn't check for updates.\n\n%1").arg(reason));
        checker->deleteLater();
    });
    checker->checkForUpdate();
}

void MainWindow::showAboutDialog()
{
    QMessageBox::about(
        this, tr("About Athena"),
        tr("<h3>Athena %1</h3>"
           "<p>A cross-distro package manager for DNF, APT, Pacman, Flatpak, and Snap.</p>"
           "<p><a href=\"%2\">%2</a></p>")
            .arg(QStringLiteral(ATHENA_VERSION), QString::fromLatin1(kProjectUrl)));
}

void MainWindow::setupMenuBar()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));

    QAction *reloadAction = fileMenu->addAction(AppIcons::refresh(), tr("&Reload Package Information"));
    reloadAction->setShortcut(QKeySequence(tr("Ctrl+R")));
    connect(reloadAction, &QAction::triggered, this, &MainWindow::reloadAllPackageInformation);

    fileMenu->addSeparator();

    QAction *quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto *editMenu = menuBar()->addMenu(tr("&Edit"));

    QAction *findAction = editMenu->addAction(AppIcons::search(), tr("&Find Package..."));
    findAction->setShortcut(QKeySequence::Find);
    connect(findAction, &QAction::triggered, this, [this]() { focusGroupTab(tr("Search")); });

    auto *packageMenu = menuBar()->addMenu(tr("&Package"));

    QAction *showUpdatesAction = packageMenu->addAction(AppIcons::update(), tr("View &Available Updates"));
    connect(showUpdatesAction, &QAction::triggered, this, [this]() { focusGroupTab(tr("Updates")); });

    QAction *cleanAction = packageMenu->addAction(AppIcons::clean(), tr("&Clean Unused Dependencies"));
    connect(cleanAction, &QAction::triggered, this, [this]() {
        if (PackageBackend *backend = currentGroupBackend())
            PackageActions::cleanUnusedDependencies(this, backend, [](bool) {});
    });

    auto *settingsMenu = menuBar()->addMenu(tr("&Settings"));

    QAction *preferencesAction = settingsMenu->addAction(tr("&Preferences"));
    connect(preferencesAction, &QAction::triggered, this, [this]() {
        PreferencesDialog dialog(this);
        dialog.exec();
    });

    QAction *repositoriesAction = settingsMenu->addAction(tr("&Repositories"));
    connect(repositoriesAction, &QAction::triggered, this, [this]() { focusGroupTab(tr("Repositories")); });

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));

    QAction *checkUpdatesAction = helpMenu->addAction(tr("Check for &Application Updates"));
    connect(checkUpdatesAction, &QAction::triggered, this, &MainWindow::checkForApplicationUpdates);

    helpMenu->addSeparator();

    QAction *reportBugAction = helpMenu->addAction(tr("&Report a Bug..."));
    connect(reportBugAction, &QAction::triggered, this,
            []() { QDesktopServices::openUrl(QUrl(QString::fromLatin1(kIssuesUrl))); });

    QAction *aboutAction = helpMenu->addAction(tr("&About Athena"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAboutDialog);

    // Group-scoped actions (Find, View Updates, Clean, Repositories) only
    // make sense while a System/Flatpak/Snap tab is active, and
    // Repositories only exists as a sub-tab for some groups — keep them
    // enabled/disabled to match whatever the user is currently looking at.
    auto updateActionStates = [this, findAction, showUpdatesAction, cleanAction, repositoriesAction]() {
        QTabWidget *group = currentGroupTabs();
        findAction->setEnabled(group != nullptr);
        showUpdatesAction->setEnabled(group != nullptr);
        cleanAction->setEnabled(currentGroupBackend() != nullptr);

        bool hasRepositories = false;
        if (group) {
            for (int i = 0; i < group->count(); ++i) {
                if (group->tabText(i) == tr("Repositories")) {
                    hasRepositories = true;
                    break;
                }
            }
        }
        repositoriesAction->setEnabled(hasRepositories);
    };
    connect(m_groupStack, &QStackedWidget::currentChanged, this, updateActionStates);
    updateActionStates();
}
