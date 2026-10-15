#include "PackageBrowser.h"

#include <algorithm>

#include <QAction>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPoint>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QTableView>
#include <QVBoxLayout>

#include "../core/AppSettings.h"
#include "../models/PackageTableModel.h"
#include "AppIcons.h"
#include "MaterialTheme.h"
#include "PackageActions.h"

namespace {
QStringList namesOf(const QVector<PackageInfo> &packages)
{
    QStringList names;
    names.reserve(packages.size());
    for (const PackageInfo &pkg : packages)
        names.append(pkg.name);
    return names;
}
} // namespace

PackageBrowser::PackageBrowser(PackageBackend *backend, Mode mode, QWidget *parent)
    : QWidget(parent), m_backend(backend), m_mode(mode)
{
    m_model = new PackageTableModel(this);
    m_proxyModel = new QSortFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setFilterKeyColumn(-1); // filter across all columns

    m_tableView = new QTableView(this);
    m_tableView->setModel(m_proxyModel);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setSortingEnabled(true);
    m_tableView->verticalHeader()->setVisible(false);
    // Flat, airy rows (no cell grid), Discover-style.
    m_tableView->setShowGrid(false);
    m_tableView->setWordWrap(false);
    m_tableView->horizontalHeader()->setHighlightSections(false);
    m_tableView->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->horizontalHeader()->setSectionResizeMode(PackageTableModel::CheckColumn, QHeaderView::Fixed);
    m_tableView->setColumnWidth(PackageTableModel::CheckColumn, 40);
    m_tableView->setColumnWidth(PackageTableModel::NameColumn, 240);
    m_tableView->setColumnWidth(PackageTableModel::InstalledVersionColumn, 150);
    m_tableView->setColumnWidth(PackageTableModel::AvailableVersionColumn, 150);
    m_tableView->setColumnWidth(PackageTableModel::ArchitectureColumn, 90);
    m_tableView->setColumnWidth(PackageTableModel::SizeColumn, 90);
    // A Material data table: a borderless card with a hairline header.
    m_tableView->setProperty("m3", QStringLiteral("table"));
    m_tableView->setFrameShape(QFrame::NoFrame);
    m_tableView->setMouseTracking(true);
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    // Off by default to match the table's existing appearance; the user
    // opts in per-column from the header's own right-click menu.
    m_tableView->setColumnHidden(PackageTableModel::ArchitectureColumn,
                                  !AppSettings::instance().showArchitectureColumn());
    m_tableView->setColumnHidden(PackageTableModel::SizeColumn, !AppSettings::instance().showSizeColumn());
    m_tableView->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    const int normalRowHeight =
        std::max(m_tableView->verticalHeader()->defaultSectionSize(), m_tableView->fontMetrics().height() + 16);
    constexpr int compactRowHeight = 22;
    m_tableView->verticalHeader()->setDefaultSectionSize(
        AppSettings::instance().compactPackageLists() ? compactRowHeight : normalRowHeight);
    connect(&AppSettings::instance(), &AppSettings::compactPackageListsChanged, this,
            [this, normalRowHeight](bool compact) {
                m_tableView->verticalHeader()->setDefaultSectionSize(compact ? compactRowHeight
                                                                              : normalRowHeight);
            });

    m_selectAllButton = new QPushButton(AppIcons::selectAll(), tr("Select All"), this);
    MaterialTheme::styleButton(m_selectAllButton, QStringLiteral("text"), QStringLiteral("select_all"));
    m_selectNoneButton = new QPushButton(AppIcons::selectNone(), tr("Select None"), this);
    MaterialTheme::styleButton(m_selectNoneButton, QStringLiteral("text"), QStringLiteral("deselect"));

    m_installButton = new QPushButton(AppIcons::install(), tr("Install Selected"), this);
    MaterialTheme::styleButton(m_installButton, QStringLiteral("filled"), QStringLiteral("download"));
    m_installButton->setEnabled(false);
    m_uninstallButton = new QPushButton(AppIcons::uninstall(), tr("Uninstall Selected"), this);
    MaterialTheme::styleButton(m_uninstallButton, QStringLiteral("danger"), QStringLiteral("delete"));
    m_uninstallButton->setEnabled(false);
    m_reinstallButton = new QPushButton(AppIcons::reinstall(), tr("Reinstall Selected"), this);
    MaterialTheme::styleButton(m_reinstallButton, QStringLiteral("tonal"), QStringLiteral("restart_alt"));
    m_reinstallButton->setEnabled(false);
    m_updateButton = new QPushButton(AppIcons::update(), tr("Update Selected"), this);
    MaterialTheme::styleButton(m_updateButton, QStringLiteral("filled"), QStringLiteral("system_update_alt"));
    m_updateButton->setEnabled(false);

    const bool updatesMode = (m_mode == Mode::Updates);
    m_installButton->setVisible(!updatesMode);
    m_uninstallButton->setVisible(!updatesMode);
    m_reinstallButton->setVisible(!updatesMode);
    m_updateButton->setVisible(updatesMode);

    auto *actionRow = new QHBoxLayout;
    actionRow->addWidget(m_selectAllButton);
    actionRow->addWidget(m_selectNoneButton);
    // The counts the old window status bar showed, now beside the actions
    // they describe.
    m_statsLabel = new QLabel(this);
    m_statsLabel->setProperty("m3", QStringLiteral("muted"));
    actionRow->addStretch(1);
    actionRow->addWidget(m_installButton);
    actionRow->addWidget(m_reinstallButton);
    actionRow->addWidget(m_uninstallButton);
    actionRow->addWidget(m_updateButton);

    m_detailTitle = new QLabel(this);
    m_detailTitle->setProperty("m3", QStringLiteral("title"));

    // Dimmed secondary text (a palette role, so it follows theme changes)
    // rather than italics.
    m_detailMeta = new QLabel(this);
    m_detailMeta->setProperty("m3", QStringLiteral("muted"));

    m_detailDescription = new QLabel(this);
    m_detailDescription->setWordWrap(true);
    m_detailDescription->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    m_detailUrl = new QLabel(this);
    m_detailUrl->setTextFormat(Qt::RichText);
    m_detailUrl->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_detailUrl->setOpenExternalLinks(true);
    m_detailUrl->hide();

    auto *detailLayout = new QVBoxLayout;
    detailLayout->addWidget(m_detailTitle);
    detailLayout->addWidget(m_detailMeta);
    detailLayout->addWidget(m_detailDescription, 1);
    detailLayout->addWidget(m_detailUrl);
    detailLayout->addStretch(1);
    auto *detailPanel = new QFrame(this);
    detailPanel->setProperty("m3", QStringLiteral("card"));
    detailLayout->setContentsMargins(20, 16, 20, 16);
    detailPanel->setLayout(detailLayout);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->setHandleWidth(12);
    splitter->addWidget(m_tableView);
    splitter->addWidget(detailPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(actionRow);
    layout->addWidget(splitter, 1);
    layout->addWidget(m_statsLabel);

    connect(m_model, &PackageTableModel::checkedChanged, this, &PackageBrowser::onCheckedChanged);
    connect(m_tableView->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            &PackageBrowser::onCurrentRowChanged);
    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &PackageBrowser::onSelectionChanged);
    connect(m_tableView, &QTableView::customContextMenuRequested, this, &PackageBrowser::showContextMenu);
    connect(m_tableView->horizontalHeader(), &QWidget::customContextMenuRequested, this,
            &PackageBrowser::showColumnMenu);
    connect(m_selectAllButton, &QPushButton::clicked, this, [this]() { m_model->checkAll(true); });
    connect(m_selectNoneButton, &QPushButton::clicked, this, [this]() { m_model->checkAll(false); });
    connect(m_installButton, &QPushButton::clicked, this, &PackageBrowser::installChecked);
    connect(m_uninstallButton, &QPushButton::clicked, this, &PackageBrowser::uninstallUnchecked);
    connect(m_reinstallButton, &QPushButton::clicked, this, &PackageBrowser::reinstallSelected);
    connect(m_updateButton, &QPushButton::clicked, this, &PackageBrowser::updateChecked);

    updateDescriptionPanel(nullptr);
}

void PackageBrowser::setPackages(const QVector<PackageInfo> &packages)
{
    m_model->setPackages(packages);
    updateDescriptionPanel(nullptr);
    onCheckedChanged();
}

void PackageBrowser::setFilterText(const QString &text)
{
    m_proxyModel->setFilterFixedString(text);
}

PackageBrowser::Stats PackageBrowser::stats() const
{
    Stats result;
    result.listed = m_model->packageCount();
    for (const PackageInfo &pkg : m_model->packages()) {
        if (pkg.installed)
            ++result.installed;
    }

    if (m_mode == Mode::Updates) {
        // Every row here is already installed; checked means "marked to
        // upgrade" rather than "marked to install".
        result.toInstallOrUpgrade = m_model->checkedNames().size();
    } else {
        for (const PackageInfo &pkg : m_model->checkedPackages()) {
            if (!pkg.installed)
                ++result.toInstallOrUpgrade;
        }
        result.toRemove = m_model->uncheckedInstalledPackages().size();
    }
    return result;
}

void PackageBrowser::setBusy(bool busy)
{
    m_busy = busy;
    m_tableView->setEnabled(!busy);
    m_selectAllButton->setEnabled(!busy);
    m_selectNoneButton->setEnabled(!busy);
    if (busy) {
        m_installButton->setEnabled(false);
        m_uninstallButton->setEnabled(false);
        m_reinstallButton->setEnabled(false);
        m_updateButton->setEnabled(false);
    } else {
        onCheckedChanged();
        onSelectionChanged();
    }
}

void PackageBrowser::onCheckedChanged()
{
    const Stats counts = stats();
    m_statsLabel->setText(m_mode == Mode::Updates
                              ? tr("%1 listed · %2 to upgrade").arg(counts.listed).arg(counts.toInstallOrUpgrade)
                              : tr("%1 listed · %2 installed · %3 to install · %4 to remove")
                                    .arg(counts.listed)
                                    .arg(counts.installed)
                                    .arg(counts.toInstallOrUpgrade)
                                    .arg(counts.toRemove));
    if (m_busy)
        return;

    const QVector<PackageInfo> checked = m_model->checkedPackages();

    if (m_mode == Mode::Updates) {
        m_updateButton->setEnabled(!checked.isEmpty());
        m_updateButton->setText(checked.size() > 1 ? tr("Update Selected (%1)").arg(checked.size())
                                                     : tr("Update Selected"));
        return;
    }

    int toInstallCount = 0;
    for (const PackageInfo &pkg : checked) {
        if (!pkg.installed)
            ++toInstallCount;
    }
    m_installButton->setEnabled(toInstallCount > 0);
    m_installButton->setText(toInstallCount > 1 ? tr("Install Selected (%1)").arg(toInstallCount)
                                                  : tr("Install Selected"));

    const int toUninstallCount = m_model->uncheckedInstalledPackages().size();
    m_uninstallButton->setEnabled(toUninstallCount > 0);
    m_uninstallButton->setText(toUninstallCount > 1 ? tr("Uninstall Selected (%1)").arg(toUninstallCount)
                                                      : tr("Uninstall Selected"));
}

QVector<PackageInfo> PackageBrowser::selectedInstalledPackages() const
{
    QVector<PackageInfo> result;
    for (const QModelIndex &proxyIndex : m_tableView->selectionModel()->selectedRows()) {
        const QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
        const PackageInfo &pkg = m_model->packageAt(sourceIndex.row());
        if (pkg.installed)
            result.append(pkg);
    }
    return result;
}

void PackageBrowser::onSelectionChanged()
{
    if (m_busy || m_mode == Mode::Updates)
        return;

    const int count = selectedInstalledPackages().size();
    m_reinstallButton->setEnabled(count > 0);
    m_reinstallButton->setText(count > 1 ? tr("Reinstall Selected (%1)").arg(count) : tr("Reinstall Selected"));
}

void PackageBrowser::onCurrentRowChanged(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous);
    if (!current.isValid()) {
        updateDescriptionPanel(nullptr);
        return;
    }
    const QModelIndex sourceIndex = m_proxyModel->mapToSource(current);
    const PackageInfo &pkg = m_model->packageAt(sourceIndex.row());
    updateDescriptionPanel(&pkg);
}

void PackageBrowser::updateDescriptionPanel(const PackageInfo *pkg)
{
    if (!pkg) {
        m_detailTitle->setText(QString());
        m_detailMeta->setText(QString());
        m_detailDescription->setText(tr("Select a package to see its details."));
        m_detailDescription->setAlignment(Qt::AlignCenter);
        m_detailUrl->hide();
        return;
    }
    m_detailDescription->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    m_detailTitle->setText(pkg->name);

    QStringList metaParts;
    metaParts << (pkg->installed ? tr("Installed: %1").arg(pkg->installedVersion) : tr("Not installed"));
    const QString latest = !pkg->availableVersion.isEmpty() ? pkg->availableVersion : pkg->installedVersion;
    if (!latest.isEmpty())
        metaParts << tr("Latest: %1").arg(latest);
    if (!pkg->architecture.isEmpty())
        metaParts << tr("Arch: %1").arg(pkg->architecture);
    if (!pkg->repository.isEmpty())
        metaParts << tr("Repository: %1").arg(pkg->repository);
    m_detailMeta->setText(metaParts.join(QStringLiteral("   ·   ")));

    m_detailDescription->setText(!pkg->longDescription.isEmpty() ? pkg->longDescription : pkg->description);

    if (pkg->homepageUrl.isEmpty()) {
        m_detailUrl->hide();
    } else {
        const QString escaped = pkg->homepageUrl.toHtmlEscaped();
        m_detailUrl->setText(tr("Homepage: <a href=\"%1\">%1</a>").arg(escaped));
        m_detailUrl->show();
    }
}

void PackageBrowser::showColumnMenu(const QPoint &pos)
{
    QMenu menu(this);

    QAction *archAction = menu.addAction(tr("Show Architecture Column"));
    archAction->setCheckable(true);
    archAction->setChecked(!m_tableView->isColumnHidden(PackageTableModel::ArchitectureColumn));

    QAction *sizeAction = menu.addAction(tr("Show Size Column"));
    sizeAction->setCheckable(true);
    sizeAction->setChecked(!m_tableView->isColumnHidden(PackageTableModel::SizeColumn));

    connect(archAction, &QAction::toggled, this, [this](bool checked) {
        m_tableView->setColumnHidden(PackageTableModel::ArchitectureColumn, !checked);
        AppSettings::instance().setShowArchitectureColumn(checked);
    });
    connect(sizeAction, &QAction::toggled, this, [this](bool checked) {
        m_tableView->setColumnHidden(PackageTableModel::SizeColumn, !checked);
        AppSettings::instance().setShowSizeColumn(checked);
    });

    menu.exec(m_tableView->horizontalHeader()->mapToGlobal(pos));
}

void PackageBrowser::installChecked()
{
    QStringList names;
    for (const PackageInfo &pkg : m_model->checkedPackages()) {
        if (!pkg.installed)
            names.append(pkg.name);
    }
    runInstall(names);
}

void PackageBrowser::uninstallUnchecked()
{
    runRemove(namesOf(m_model->uncheckedInstalledPackages()));
}

void PackageBrowser::reinstallSelected()
{
    runReinstall(namesOf(selectedInstalledPackages()));
}

void PackageBrowser::updateChecked()
{
    runUpgrade(namesOf(m_model->checkedPackages()));
}

void PackageBrowser::showContextMenu(const QPoint &pos)
{
    // The context menu acts on whatever rows are highlighted (right-click
    // target), not the checkboxes — those represent desired install state,
    // not a pending-action selection.
    QVector<PackageInfo> targeted;
    for (const QModelIndex &proxyIndex : m_tableView->selectionModel()->selectedRows()) {
        const QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
        targeted.append(m_model->packageAt(sourceIndex.row()));
    }
    if (targeted.isEmpty()) {
        const QModelIndex indexUnderCursor = m_tableView->indexAt(pos);
        if (indexUnderCursor.isValid()) {
            const QModelIndex sourceIndex = m_proxyModel->mapToSource(indexUnderCursor);
            targeted = {m_model->packageAt(sourceIndex.row())};
        }
    }
    if (targeted.isEmpty())
        return;

    QMenu menu(this);

    if (m_mode == Mode::Updates) {
        const QStringList names = namesOf(targeted);
        QAction *action = menu.addAction(names.size() > 1 ? tr("Update %1 Packages").arg(names.size())
                                                            : tr("Update %1").arg(names.first()));
        connect(action, &QAction::triggered, this, [this, names]() { runUpgrade(names); });
        menu.exec(m_tableView->viewport()->mapToGlobal(pos));
        return;
    }

    QStringList toInstall;
    QStringList toUninstall;
    for (const PackageInfo &pkg : targeted) {
        if (pkg.installed)
            toUninstall.append(pkg.name);
        else
            toInstall.append(pkg.name);
    }

    if (!toInstall.isEmpty()) {
        QAction *action = menu.addAction(
            toInstall.size() > 1 ? tr("Install %1 Packages").arg(toInstall.size()) : tr("Install %1").arg(toInstall.first()));
        connect(action, &QAction::triggered, this, [this, toInstall]() { runInstall(toInstall); });
    }
    if (!toUninstall.isEmpty()) {
        QAction *reinstallAction = menu.addAction(toUninstall.size() > 1
                                                        ? tr("Reinstall %1 Packages").arg(toUninstall.size())
                                                        : tr("Reinstall %1").arg(toUninstall.first()));
        connect(reinstallAction, &QAction::triggered, this, [this, toUninstall]() { runReinstall(toUninstall); });

        QAction *uninstallAction = menu.addAction(toUninstall.size() > 1
                                                        ? tr("Uninstall %1 Packages").arg(toUninstall.size())
                                                        : tr("Uninstall %1").arg(toUninstall.first()));
        connect(uninstallAction, &QAction::triggered, this, [this, toUninstall]() { runRemove(toUninstall); });
    }
    if (menu.isEmpty())
        return;

    menu.exec(m_tableView->viewport()->mapToGlobal(pos));
}

void PackageBrowser::runInstall(const QStringList &names)
{
    if (names.isEmpty())
        return;
    setBusy(true);
    PackageActions::installPackages(this, m_backend, names, [this](bool success) {
        if (success)
            emit refreshRequested();
        else
            setBusy(false);
    });
}

void PackageBrowser::runRemove(const QStringList &names)
{
    if (names.isEmpty())
        return;
    setBusy(true);
    PackageActions::removePackages(this, m_backend, names, [this](bool success) {
        if (success)
            emit refreshRequested();
        else
            setBusy(false);
    });
}

void PackageBrowser::runReinstall(const QStringList &names)
{
    if (names.isEmpty())
        return;
    setBusy(true);
    PackageActions::reinstallPackages(this, m_backend, names, [this](bool success) {
        if (success)
            emit refreshRequested();
        else
            setBusy(false);
    });
}

void PackageBrowser::runUpgrade(const QStringList &names)
{
    if (names.isEmpty())
        return;
    setBusy(true);

    PackageActions::upgradePackages(this, m_backend, names, [this](bool success) {
        if (success)
            emit refreshRequested();
        else
            setBusy(false);
    });
}
