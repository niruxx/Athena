#include "GroupsPage.h"

#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "AppIcons.h"
#include "PackageBrowser.h"

namespace {
constexpr int GroupIdRole = Qt::UserRole;
constexpr int GroupIsMetaRole = Qt::UserRole + 1;
} // namespace

GroupsPage::GroupsPage(PackageBackend *backend, QWidget *parent) : QWidget(parent), m_backend(backend)
{
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("Category"), tr("Installed")});
    // A fixed pixel width truncated longer category/environment names
    // regardless of how wide the splitter panel actually was; resize to
    // fit whatever's currently in the tree instead.
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);

    m_categoryTitle = new QLabel(this);
    QFont titleFont = m_categoryTitle->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    m_categoryTitle->setFont(titleFont);

    m_categoryDescription = new QLabel(this);
    m_categoryDescription->setWordWrap(true);

    m_browser = new PackageBrowser(backend, PackageBrowser::Mode::InstallRemove, this);

    auto *rightLayout = new QVBoxLayout;
    rightLayout->addWidget(m_categoryTitle);
    rightLayout->addWidget(m_categoryDescription);
    rightLayout->addWidget(m_browser, 1);
    auto *rightPanel = new QWidget(this);
    rightPanel->setLayout(rightLayout);

    auto *splitter = new QSplitter(this);
    splitter->addWidget(m_tree);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({320, 680});

    m_refreshButton = new QPushButton(AppIcons::refresh(), tr("Refresh"), this);
    m_statusLabel = new QLabel(tr("Loading categories..."), this);

    auto *topRow = new QHBoxLayout;
    topRow->addWidget(m_statusLabel, 1);
    topRow->addWidget(m_refreshButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(topRow);
    layout->addWidget(splitter, 1);

    connect(m_refreshButton, &QPushButton::clicked, this, &GroupsPage::refresh);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &GroupsPage::onGroupSelected);
    connect(m_browser, &PackageBrowser::refreshRequested, this, &GroupsPage::refresh);
    connect(&m_listWatcher, &QFutureWatcher<QVector<PackageGroupInfo>>::finished, this,
            &GroupsPage::onGroupsLoaded);
    connect(&m_contentsWatcher, &QFutureWatcher<GroupContentsResult>::finished, this,
            &GroupsPage::onGroupContentsLoaded);

    m_categoryDescription->setText(tr("Select a category on the left to browse its packages."));

    refresh();
}

void GroupsPage::refresh()
{
    if (!m_backend)
        return;

    m_refreshButton->setEnabled(false);
    m_statusLabel->setText(tr("Loading categories..."));

    PackageBackend *backend = m_backend;
    QFuture<QVector<PackageGroupInfo>> future = QtConcurrent::run([backend]() { return backend->listGroups(); });
    m_listWatcher.setFuture(future);
}

void GroupsPage::onGroupsLoaded()
{
    const QVector<PackageGroupInfo> groups = m_listWatcher.result();

    const QString previouslySelectedId =
        m_tree->currentItem() ? m_tree->currentItem()->data(0, GroupIdRole).toString() : QString();

    m_tree->clear();

    auto *groupRoot = new QTreeWidgetItem(m_tree, {tr("Groups"), QString()});
    groupRoot->setFlags(Qt::ItemIsEnabled);

    QTreeWidgetItem *itemToReselect = nullptr;
    for (const PackageGroupInfo &group : groups) {
        auto *item = new QTreeWidgetItem(groupRoot, {group.name, group.installed ? tr("Yes") : tr("No")});
        item->setData(0, GroupIdRole, group.id);
        item->setData(0, GroupIsMetaRole, group.isMeta);
        if (!previouslySelectedId.isEmpty() && group.id == previouslySelectedId)
            itemToReselect = item;
    }

    m_tree->expandAll();
    m_statusLabel->setText(tr("%1 categories").arg(groups.size()));
    m_refreshButton->setEnabled(true);

    if (itemToReselect)
        m_tree->setCurrentItem(itemToReselect); // re-triggers onGroupSelected to reload contents

    if (groups.isEmpty()) {
        m_categoryTitle->setText(QString());
        m_categoryDescription->setText(tr("No groups reported by this backend."));
        m_browser->setPackages({});
    }
}

void GroupsPage::onGroupSelected(QTreeWidgetItem *current, QTreeWidgetItem *previous)
{
    Q_UNUSED(previous);
    if (!current || !m_backend)
        return;

    const QVariant idData = current->data(0, GroupIdRole);
    if (!idData.isValid())
        return; // the "Groups" section header, not a real item

    const QString groupId = idData.toString();
    const bool isMeta = current->data(0, GroupIsMetaRole).toBool();

    m_categoryTitle->setText(current->text(0));
    m_categoryDescription->setText(tr("Loading..."));
    m_browser->setBusy(true);

    PackageBackend *backend = m_backend;
    QFuture<GroupContentsResult> future = QtConcurrent::run([backend, groupId, isMeta]() {
        GroupContentsResult result;
        const PackageGroupInfo group = backend->groupDetails(groupId, isMeta);
        result.description = group.description;

        QStringList packageNames;
        if (!group.subGroups.isEmpty()) {
            // A meta-group's contents are other groups; flatten each
            // sub-group's packages into one combined list rather than
            // showing another level of categories.
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

        result.packages = backend->packageDetails(packageNames);
        return result;
    });
    m_contentsWatcher.setFuture(future);
}

void GroupsPage::onGroupContentsLoaded()
{
    const GroupContentsResult result = m_contentsWatcher.result();
    m_categoryDescription->setText(result.description);
    m_browser->setPackages(result.packages);
    m_browser->setBusy(false);
}
