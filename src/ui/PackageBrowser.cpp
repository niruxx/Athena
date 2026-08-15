#include "PackageBrowser.h"

#include <QAction>
#include <QFont>
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
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setSortingEnabled(true);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->horizontalHeader()->setSectionResizeMode(PackageTableModel::CheckColumn, QHeaderView::Fixed);
    m_tableView->setColumnWidth(PackageTableModel::CheckColumn, 28);
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    const int normalRowHeight = m_tableView->verticalHeader()->defaultSectionSize();
    constexpr int compactRowHeight = 22;
    if (AppSettings::instance().compactPackageLists())
        m_tableView->verticalHeader()->setDefaultSectionSize(compactRowHeight);
    connect(&AppSettings::instance(), &AppSettings::compactPackageListsChanged, this,
            [this, normalRowHeight](bool compact) {
                m_tableView->verticalHeader()->setDefaultSectionSize(compact ? compactRowHeight
                                                                              : normalRowHeight);
            });

    m_selectAllButton = new QPushButton(AppIcons::selectAll(), tr("Select All"), this);
    m_selectNoneButton = new QPushButton(AppIcons::selectNone(), tr("Select None"), this);

    m_installButton = new QPushButton(AppIcons::install(), tr("Install Selected"), this);
    m_installButton->setEnabled(false);
    m_uninstallButton = new QPushButton(AppIcons::uninstall(), tr("Uninstall Selected"), this);
    m_uninstallButton->setEnabled(false);
    m_reinstallButton = new QPushButton(AppIcons::reinstall(), tr("Reinstall Selected"), this);
    m_reinstallButton->setEnabled(false);
    m_updateButton = new QPushButton(AppIcons::update(), tr("Update Selected"), this);
    m_updateButton->setEnabled(false);

    const bool updatesMode = (m_mode == Mode::Updates);
    m_installButton->setVisible(!updatesMode);
    m_uninstallButton->setVisible(!updatesMode);
    m_reinstallButton->setVisible(!updatesMode);
    m_updateButton->setVisible(updatesMode);

    auto *actionRow = new QHBoxLayout;
    actionRow->addWidget(m_selectAllButton);
    actionRow->addWidget(m_selectNoneButton);
    actionRow->addStretch(1);
    actionRow->addWidget(m_installButton);
    actionRow->addWidget(m_reinstallButton);
    actionRow->addWidget(m_uninstallButton);
    actionRow->addWidget(m_updateButton);

    m_detailTitle = new QLabel(this);
    QFont titleFont = m_detailTitle->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    m_detailTitle->setFont(titleFont);

    m_detailMeta = new QLabel(this);
    QFont metaFont = m_detailMeta->font();
    metaFont.setItalic(true);
    m_detailMeta->setFont(metaFont);

    m_detailDescription = new QLabel(this);
    m_detailDescription->setWordWrap(true);
    m_detailDescription->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    auto *detailLayout = new QVBoxLayout;
    detailLayout->addWidget(m_detailTitle);
    detailLayout->addWidget(m_detailMeta);
    detailLayout->addWidget(m_detailDescription, 1);
    detailLayout->addStretch(1);
    auto *detailPanel = new QWidget(this);
    detailPanel->setLayout(detailLayout);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(m_tableView);
    splitter->addWidget(detailPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(actionRow);
    layout->addWidget(splitter, 1);

    connect(m_model, &PackageTableModel::checkedChanged, this, &PackageBrowser::onCheckedChanged);
    connect(m_tableView->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            &PackageBrowser::onCurrentRowChanged);
    connect(m_tableView, &QTableView::customContextMenuRequested, this, &PackageBrowser::showContextMenu);
    connect(m_selectAllButton, &QPushButton::clicked, this, [this]() { m_model->checkAll(true); });
    connect(m_selectNoneButton, &QPushButton::clicked, this, [this]() { m_model->checkAll(false); });
    connect(m_installButton, &QPushButton::clicked, this, &PackageBrowser::installChecked);
    connect(m_uninstallButton, &QPushButton::clicked, this, &PackageBrowser::uninstallChecked);
    connect(m_reinstallButton, &QPushButton::clicked, this, &PackageBrowser::reinstallChecked);
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
    }
}

void PackageBrowser::onCheckedChanged()
{
    if (m_busy)
        return;

    const QVector<PackageInfo> checked = m_model->checkedPackages();

    if (m_mode == Mode::Updates) {
        m_updateButton->setEnabled(!checked.isEmpty());
        m_updateButton->setText(checked.size() > 1 ? tr("Update Selected (%1)").arg(checked.size())
                                                     : tr("Update Selected"));
        return;
    }

    bool anyNotInstalled = false;
    bool anyInstalled = false;
    for (const PackageInfo &pkg : checked) {
        if (pkg.installed)
            anyInstalled = true;
        else
            anyNotInstalled = true;
    }
    m_installButton->setEnabled(anyNotInstalled);
    m_installButton->setText(anyNotInstalled && checked.size() > 1 ? tr("Install Selected (%1)").arg(checked.size())
                                                                    : tr("Install Selected"));
    m_uninstallButton->setEnabled(anyInstalled);
    m_uninstallButton->setText(anyInstalled && checked.size() > 1
                                    ? tr("Uninstall Selected (%1)").arg(checked.size())
                                    : tr("Uninstall Selected"));
    m_reinstallButton->setEnabled(anyInstalled);
    m_reinstallButton->setText(anyInstalled && checked.size() > 1
                                    ? tr("Reinstall Selected (%1)").arg(checked.size())
                                    : tr("Reinstall Selected"));
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
        return;
    }

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
}

void PackageBrowser::installChecked()
{
    runInstall(namesOf(m_model->checkedPackages()));
}

void PackageBrowser::uninstallChecked()
{
    QStringList names;
    for (const PackageInfo &pkg : m_model->checkedPackages()) {
        if (pkg.installed)
            names.append(pkg.name);
    }
    runRemove(names);
}

void PackageBrowser::reinstallChecked()
{
    QStringList names;
    for (const PackageInfo &pkg : m_model->checkedPackages()) {
        if (pkg.installed)
            names.append(pkg.name);
    }
    runReinstall(names);
}

void PackageBrowser::updateChecked()
{
    runUpgrade(namesOf(m_model->checkedPackages()));
}

void PackageBrowser::showContextMenu(const QPoint &pos)
{
    QVector<PackageInfo> checked = m_model->checkedPackages();

    if (checked.isEmpty()) {
        const QModelIndex indexUnderCursor = m_tableView->indexAt(pos);
        if (indexUnderCursor.isValid()) {
            const QModelIndex sourceIndex = m_proxyModel->mapToSource(indexUnderCursor);
            checked = {m_model->packageAt(sourceIndex.row())};
        }
    }
    if (checked.isEmpty())
        return;

    QMenu menu(this);

    if (m_mode == Mode::Updates) {
        const QStringList names = namesOf(checked);
        QAction *action = menu.addAction(names.size() > 1 ? tr("Update %1 Packages").arg(names.size())
                                                            : tr("Update %1").arg(names.first()));
        connect(action, &QAction::triggered, this, [this, names]() { runUpgrade(names); });
        menu.exec(m_tableView->viewport()->mapToGlobal(pos));
        return;
    }

    QStringList toInstall;
    QStringList toUninstall;
    for (const PackageInfo &pkg : checked) {
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

    PackageBackend *backend = m_backend;
    const QString verb = names.size() == 1 ? tr("Update") : tr("Update packages");
    const QString confirmText =
        tr("Update %1?").arg(names.size() > 10 ? tr("%1 packages").arg(names.size()) : names.join(", "));

    PackageActions::confirmAndRun(
        this, verb, confirmText, [backend, names]() { return backend->upgradePackages(names); },
        [this](bool success) {
            if (success)
                emit refreshRequested();
            else
                setBusy(false);
        });
}
