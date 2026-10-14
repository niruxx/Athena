#include "ToolsDialogs.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "../core/AppSettings.h"
#include "MaterialTheme.h"
#include "TerminalOutputDialog.h"

// ---------------------------------------------------------------- Cleanup

CleanupDialog::CleanupDialog(const QString &nativeKey, PackageBackend *backend, QWidget *parent) : M3Dialog(parent)
{
    setHeadline(tr("Free up space"), QStringLiteral("cleaning_services"));
    setSupportingText(tr("Choose what to clear. Nothing is removed until you confirm, and you'll see every command as it runs."));
    setMinimumWidth(560);

    m_sizeLabel = new QLabel(tr("Measuring the package cache…"), this);
    m_sizeLabel->setProperty("m3", QStringLiteral("titleM"));
    body()->addWidget(m_sizeLabel);

    m_options = SystemTools::cacheCleanupOptions(nativeKey);
    for (int i = 0; i < m_options.size(); ++i) {
        auto *check = new QCheckBox(m_options[i].label, this);
        check->setChecked(i == 0);
        auto *detail = new QLabel(m_options[i].detail, this);
        detail->setProperty("m3", QStringLiteral("muted"));
        detail->setWordWrap(true);
        detail->setContentsMargins(26, 0, 0, 0);
        body()->addWidget(check);
        body()->addWidget(detail);
        m_optionChecks.append(check);
    }
    m_orphansCheck = new QCheckBox(tr("Dependencies nothing needs anymore"), this);
    m_orphansCheck->setChecked(true);
    m_orphansCheck->setEnabled(false);
    body()->addWidget(m_orphansCheck);

    QPushButton *cancel = addAction(tr("Cancel"), QStringLiteral("text"));
    m_runButton = addAction(tr("Clean up"), QStringLiteral("filled"), QStringLiteral("cleaning_services"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_runButton, &QPushButton::clicked, this, &QDialog::accept);

    const QString cacheDir = SystemTools::packageCacheDir(nativeKey);
    connect(&m_watcher, &QFutureWatcher<Scan>::finished, this, [this]() {
        m_scan = m_watcher.result();
        m_sizeLabel->setText(m_scan.cacheBytes >= 0 ? tr("The package cache is using %1").arg(QLocale().formattedDataSize(m_scan.cacheBytes))
                                                    : tr("The package cache couldn't be measured"));
        const bool any = !m_scan.orphanCommands.isEmpty();
        m_orphansCheck->setEnabled(any);
        m_orphansCheck->setChecked(any);
        m_orphansCheck->setText(!any                  ? tr("No dependencies are left over")
                                : m_scan.orphanCount > 0 ? tr("%n dependency package(s) nothing needs anymore", "", m_scan.orphanCount)
                                                         : tr("Dependencies nothing needs anymore"));
    });
    m_watcher.setFuture(QtConcurrent::run([backend, cacheDir]() {
        Scan scan;
        scan.cacheBytes = SystemTools::directorySize(cacheDir);
        if (backend) {
            scan.orphanCommands = backend->cleanUnusedDependenciesCommands();
            // pacman lists the packages it removes ("pkexec pacman -R --noconfirm a b c").
            if (scan.orphanCommands.size() == 1 && scan.orphanCommands.first().args.value(1) == QLatin1String("pacman"))
                scan.orphanCount = std::max(0, int(scan.orphanCommands.first().args.size()) - 3);
        }
        return scan;
    }));
}

bool CleanupDialog::runModal()
{
    if (exec() != QDialog::Accepted)
        return false;
    QVector<ProcessRunner::Command> commands;
    for (int i = 0; i < m_options.size(); ++i) {
        if (m_optionChecks[i]->isChecked())
            commands += m_options[i].commands;
    }
    if (m_orphansCheck->isChecked())
        commands += m_scan.orphanCommands;
    if (commands.isEmpty())
        return false;
    TerminalOutputDialog terminal(tr("Free up space"), parentWidget());
    return terminal.runCommandsModal(commands, AppSettings::instance().autoCloseTerminalOnSuccess());
}

// ---------------------------------------------------------------- News

NewsDialog::NewsDialog(const SystemTools::NewsItem &item, QWidget *parent) : M3Dialog(parent)
{
    setHeadline(item.title, item.manualIntervention ? QStringLiteral("warning") : QStringLiteral("campaign"));
    setSupportingText(tr("Arch Linux news · %1").arg(QLocale().toString(item.published.date(), QLocale::LongFormat)));
    setResizable(true);
    resize(720, 600);

    auto *view = new QTextBrowser(this);
    view->setHtml(item.html);
    view->setOpenExternalLinks(true);
    view->setMinimumHeight(260);
    body()->addWidget(view, 1);

    QPushButton *open = addAction(tr("Open on archlinux.org"), QStringLiteral("text"), QStringLiteral("open_in_new"));
    QPushButton *done = addAction(tr("Done"), QStringLiteral("filled"));
    const QString link = item.link;
    connect(open, &QPushButton::clicked, this, [link]() { QDesktopServices::openUrl(QUrl(link)); });
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
}

// ---------------------------------------------------------------- Downgrade

DowngradeDialog::DowngradeDialog(const QString &package, const QString &installedVersion, QWidget *parent)
    : M3Dialog(parent), m_package(package)
{
    setHeadline(tr("Install another version of %1").arg(package), QStringLiteral("history"));
    setSupportingText(tr("Installed: %1. Older versions can break packages that need a newer one, and the next system "
                         "update will upgrade it again.")
                          .arg(installedVersion));
    setResizable(true);
    setMinimumWidth(560);
    resize(600, 560);

    m_status = new QLabel(tr("Looking in the package cache and the Arch Linux Archive…"), this);
    m_status->setProperty("m3", QStringLiteral("muted"));
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(6);
    m_list = new QListWidget(this);
    m_list->setMinimumHeight(240);
    body()->addWidget(m_status);
    body()->addWidget(m_progress);
    body()->addWidget(m_list, 1);

    QPushButton *cancel = addAction(tr("Cancel"), QStringLiteral("text"));
    m_installButton = addAction(tr("Install this version"), QStringLiteral("filled"), QStringLiteral("download"));
    m_installButton->setEnabled(false);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_installButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) { m_installButton->setEnabled(row >= 0); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);

    connect(&m_watcher, &QFutureWatcher<QVector<SystemTools::PackageVersion>>::finished, this, [this]() {
        m_versions = m_watcher.result();
        m_progress->hide();
        m_status->setText(m_versions.isEmpty() ? tr("No other versions found in the cache or the archive.")
                                               : tr("%n version(s) available", "", m_versions.size()));
        for (const SystemTools::PackageVersion &version : m_versions) {
            auto *item = new QListWidgetItem(
                MaterialTheme::icon(version.cached ? QStringLiteral("storage") : QStringLiteral("public"),
                                    MaterialTheme::current().onSurfaceVariant, 18),
                tr("%1  ·  %2").arg(version.version, version.cached ? tr("in your package cache") : tr("from the Arch Linux Archive")),
                m_list);
            item->setToolTip(version.location);
        }
    });
    m_watcher.setFuture(QtConcurrent::run(&SystemTools::downgradeCandidates, package, installedVersion));
}

bool DowngradeDialog::runModal()
{
    if (exec() != QDialog::Accepted || m_list->currentRow() < 0)
        return false;
    const SystemTools::PackageVersion version = m_versions[m_list->currentRow()];
    TerminalOutputDialog terminal(tr("Install %1 %2").arg(m_package, version.version), parentWidget());
    QVector<ProcessRunner::Command> commands;
    if (AppSettings::instance().snapshotBeforeTransactions())
        commands += SystemTools::snapshotCommands(QStringLiteral("Athena: before installing %1 %2").arg(m_package, version.version));
    commands += SystemTools::installVersionCommands(version);
    return terminal.runCommandsModal(commands, AppSettings::instance().autoCloseTerminalOnSuccess());
}
