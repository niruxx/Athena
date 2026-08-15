#include "InstalledPage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "AppIcons.h"
#include "PackageActions.h"
#include "PackageBrowser.h"

namespace {
constexpr int CategoryIdRole = Qt::UserRole;
constexpr int CategoryIsMetaRole = Qt::UserRole + 1;
} // namespace

InstalledPage::InstalledPage(PackageBackend *backend, QWidget *parent)
    : QWidget(parent), m_backend(backend)
{
    m_categoryTree = new QTreeWidget(this);
    m_categoryTree->setHeaderHidden(true);
    m_categoryTree->setColumnCount(1);
    auto *allItem = new QTreeWidgetItem(m_categoryTree, {tr("All Installed Packages")});
    m_categoryTree->setCurrentItem(allItem);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Filter installed packages..."));

    m_refreshButton = new QPushButton(AppIcons::refresh(), tr("Refresh"), this);
    m_cleanButton = new QPushButton(AppIcons::clean(), tr("Clean Left Behind Dependencies"), this);

    m_browser = new PackageBrowser(backend, PackageBrowser::Mode::InstallRemove, this);

    m_statusLabel = new QLabel(tr("Loading installed packages..."), this);

    auto *topRow = new QHBoxLayout;
    topRow->addWidget(m_filterEdit, 1);
    topRow->addWidget(m_cleanButton);
    topRow->addWidget(m_refreshButton);

    auto *rightLayout = new QVBoxLayout;
    rightLayout->addLayout(topRow);
    rightLayout->addWidget(m_browser, 1);
    rightLayout->addWidget(m_statusLabel);
    auto *rightPanel = new QWidget(this);
    rightPanel->setLayout(rightLayout);

    auto *splitter = new QSplitter(this);
    splitter->addWidget(m_categoryTree);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({320, 680});

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(splitter, 1);

    connect(m_filterEdit, &QLineEdit::textChanged, m_browser, &PackageBrowser::setFilterText);
    connect(m_refreshButton, &QPushButton::clicked, this, &InstalledPage::refresh);
    connect(m_cleanButton, &QPushButton::clicked, this, &InstalledPage::onCleanClicked);
    connect(m_browser, &PackageBrowser::refreshRequested, this, &InstalledPage::refresh);
    connect(&m_watcher, &QFutureWatcher<QVector<PackageInfo>>::finished, this, &InstalledPage::onLoaded);
    connect(&m_categoriesWatcher, &QFutureWatcher<QVector<PackageGroupInfo>>::finished, this,
            &InstalledPage::onCategoriesLoaded);
    connect(&m_categoryContentsWatcher, &QFutureWatcher<QVector<PackageInfo>>::finished, this,
            &InstalledPage::onCategoryContentsLoaded);
    connect(m_categoryTree, &QTreeWidget::currentItemChanged, this, &InstalledPage::onCategorySelected);

    refresh();
    refreshCategories();
}

void InstalledPage::refresh()
{
    if (!m_backend)
        return;

    setBusy(true);
    m_statusLabel->setText(tr("Loading installed packages..."));

    PackageBackend *backend = m_backend;
    QFuture<QVector<PackageInfo>> future =
        QtConcurrent::run([backend]() { return backend->listInstalled(); });
    m_watcher.setFuture(future);
}

void InstalledPage::onLoaded()
{
    m_allPackages = m_watcher.result();
    m_statusLabel->setText(tr("%1 installed packages").arg(m_allPackages.size()));
    applyCategoryFilter();
    setBusy(false);
}

void InstalledPage::refreshCategories()
{
    if (!m_backend)
        return;

    PackageBackend *backend = m_backend;
    QFuture<QVector<PackageGroupInfo>> future = QtConcurrent::run([backend]() { return backend->listGroups(); });
    m_categoriesWatcher.setFuture(future);
}

void InstalledPage::onCategoriesLoaded()
{
    const QVector<PackageGroupInfo> groups = m_categoriesWatcher.result();
    if (groups.isEmpty())
        return;

    QTreeWidgetItem *allItem = m_categoryTree->topLevelItem(0);
    auto *categoriesRoot = new QTreeWidgetItem(m_categoryTree, {tr("Categories")});
    categoriesRoot->setFlags(Qt::ItemIsEnabled);

    for (const PackageGroupInfo &group : groups) {
        auto *item = new QTreeWidgetItem(categoriesRoot, {group.name});
        item->setData(0, CategoryIdRole, group.id);
        item->setData(0, CategoryIsMetaRole, group.isMeta);
    }

    m_categoryTree->expandItem(categoriesRoot);
    m_categoryTree->setCurrentItem(allItem);
}

void InstalledPage::onCategorySelected(QTreeWidgetItem *current, QTreeWidgetItem *previous)
{
    Q_UNUSED(previous);
    if (!current || !m_backend)
        return;

    const QVariant idData = current->data(0, CategoryIdRole);
    if (!idData.isValid()) {
        // "All Installed Packages" or the "Categories" section header.
        m_categoryFilterActive = false;
        applyCategoryFilter();
        return;
    }

    const QString categoryId = idData.toString();
    const bool isMeta = current->data(0, CategoryIsMetaRole).toBool();

    m_browser->setBusy(true);

    PackageBackend *backend = m_backend;
    QFuture<QVector<PackageInfo>> future = QtConcurrent::run([backend, categoryId, isMeta]() {
        const PackageGroupInfo group = backend->groupDetails(categoryId, isMeta);

        QStringList packageNames;
        if (!group.subGroups.isEmpty()) {
            for (const QString &subGroupId : group.subGroups) {
                const PackageGroupInfo subGroup = backend->groupDetails(subGroupId, false);
                for (const QString &pkgName : subGroup.packages) {
                    if (!packageNames.contains(pkgName))
                        packageNames.append(pkgName);
                }
            }
        } else {
            packageNames = group.packages;
        }

        // Resolve via the backend (same as GroupsPage) rather than matching
        // names against a separately-loaded list, then keep only the
        // installed ones — this is the Installed tab, after all.
        QVector<PackageInfo> installedOnly;
        for (const PackageInfo &pkg : backend->packageDetails(packageNames)) {
            if (pkg.installed)
                installedOnly.append(pkg);
        }
        return installedOnly;
    });
    m_categoryContentsWatcher.setFuture(future);
}

void InstalledPage::onCategoryContentsLoaded()
{
    m_browser->setPackages(m_categoryContentsWatcher.result());
    m_browser->setBusy(false);
    m_categoryFilterActive = true;
}

void InstalledPage::applyCategoryFilter()
{
    if (m_categoryFilterActive) {
        // A category is selected but its contents haven't been (re)computed
        // for the freshly reloaded package list yet; onCategorySelected's
        // background lookup will call back into onCategoryContentsLoaded.
        onCategorySelected(m_categoryTree->currentItem(), nullptr);
        return;
    }
    m_browser->setPackages(m_allPackages);
}

void InstalledPage::onCleanClicked()
{
    if (!m_backend)
        return;

    setBusy(true);
    m_statusLabel->setText(tr("Cleaning up unused dependencies..."));

    PackageActions::cleanUnusedDependencies(this, m_backend, [this](bool success) {
        if (success)
            refresh();
        else
            setBusy(false);
    });
}

void InstalledPage::setBusy(bool busy)
{
    m_refreshButton->setEnabled(!busy);
    m_cleanButton->setEnabled(!busy);
    m_filterEdit->setEnabled(!busy);
    m_categoryTree->setEnabled(!busy);
    m_browser->setBusy(busy);
}
