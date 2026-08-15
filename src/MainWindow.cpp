#include "MainWindow.h"

#include <QGuiApplication>
#include <QLabel>
#include <QScreen>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "core/AppSettings.h"
#include "core/BackendFactory.h"
#include "core/GitHubReleaseChecker.h"
#include "core/backends/FlatpakBackend.h"
#include "core/backends/SnapBackend.h"
#include "ui/FirstRunDialog.h"
#include "ui/GroupsPage.h"
#include "ui/HistoryPage.h"
#include "ui/InstalledPage.h"
#include "ui/RepositoriesPage.h"
#include "ui/SearchPage.h"
#include "ui/SettingsPage.h"
#include "ui/UpdateBannerWidget.h"
#include "ui/UpdatesPage.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("Distore"));
    resize(1000, 650);

    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        move(available.x() + (available.width() - width()) / 2,
             available.y() + (available.height() - height()) / 2);
    }

    m_backend = BackendFactory::createForHostSystem();

    auto *tabs = new QTabWidget(this);
    int systemTabIndex = -1;
    int flatpakTabIndex = -1;
    int snapTabIndex = -1;

    if (m_backend) {
        auto *systemTabs = new QTabWidget(tabs);
        systemTabs->addTab(new InstalledPage(m_backend.get(), systemTabs), tr("Installed"));
        systemTabs->addTab(new UpdatesPage(m_backend.get(), systemTabs), tr("Updates"));
        systemTabs->addTab(new SearchPage(m_backend.get(), systemTabs), tr("Search"));
        systemTabs->addTab(new GroupsPage(m_backend.get(), systemTabs), tr("Groups"));
        systemTabs->addTab(
            new RepositoriesPage({{m_backend.get(), tr("System")}}, systemTabs), tr("Repositories"));
        systemTabs->addTab(new HistoryPage(m_backend.get(), systemTabs), tr("History"));
        systemTabIndex = tabs->addTab(systemTabs, tr("System"));
        statusBar()->showMessage(tr("Backend: %1").arg(m_backend->backendName()));
    } else {
        auto *label = new QLabel(
            tr("No supported package manager (dnf, apt, or pacman) was found on this system."), tabs);
        label->setAlignment(Qt::AlignCenter);
        label->setWordWrap(true);
        systemTabIndex = tabs->addTab(label, tr("System"));
    }

    auto flatpakBackend = std::make_unique<FlatpakBackend>();
    if (flatpakBackend->isAvailable()) {
        m_flatpakBackend = std::move(flatpakBackend);

        auto *flatpakTabs = new QTabWidget(tabs);
        flatpakTabs->addTab(new InstalledPage(m_flatpakBackend.get(), flatpakTabs), tr("Installed"));
        flatpakTabs->addTab(new UpdatesPage(m_flatpakBackend.get(), flatpakTabs), tr("Updates"));
        flatpakTabs->addTab(new SearchPage(m_flatpakBackend.get(), flatpakTabs), tr("Search"));
        flatpakTabs->addTab(
            new RepositoriesPage({{m_flatpakBackend.get(), tr("Flatpak")}}, flatpakTabs), tr("Repositories"));
        flatpakTabs->addTab(new HistoryPage(m_flatpakBackend.get(), flatpakTabs), tr("History"));
        flatpakTabIndex = tabs->addTab(flatpakTabs, tr("Flatpak"));
    }

    auto snapBackend = std::make_unique<SnapBackend>();
    if (snapBackend->isAvailable()) {
        m_snapBackend = std::move(snapBackend);

        auto *snapTabs = new QTabWidget(tabs);
        snapTabs->addTab(new InstalledPage(m_snapBackend.get(), snapTabs), tr("Installed"));
        snapTabs->addTab(new UpdatesPage(m_snapBackend.get(), snapTabs), tr("Updates"));
        snapTabs->addTab(new SearchPage(m_snapBackend.get(), snapTabs), tr("Search"));
        snapTabs->addTab(new HistoryPage(m_snapBackend.get(), snapTabs), tr("History"));
        snapTabIndex = tabs->addTab(snapTabs, tr("Snap"));
    }

    tabs->addTab(new SettingsPage(tabs), tr("Settings"));

    m_updateBanner = new UpdateBannerWidget(this);

    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(m_updateBanner);
    centralLayout->addWidget(tabs, 1);
    setCentralWidget(central);

    const StartupTab requestedStartupTab = AppSettings::instance().startupTab();
    int startupIndex = systemTabIndex;
    if (requestedStartupTab == StartupTab::Flatpak && flatpakTabIndex >= 0)
        startupIndex = flatpakTabIndex;
    else if (requestedStartupTab == StartupTab::Snap && snapTabIndex >= 0)
        startupIndex = snapTabIndex;
    if (startupIndex >= 0)
        tabs->setCurrentIndex(startupIndex);

    if (AppSettings::instance().checkForAppUpdatesOnStartup()) {
        auto *checker = new GitHubReleaseChecker(this);
        connect(checker, &GitHubReleaseChecker::updateAvailable, this,
                [this](const QString &version, const QString &htmlUrl) {
                    m_updateBanner->showUpdate(version, htmlUrl);
                });
        checker->checkForUpdate();
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
