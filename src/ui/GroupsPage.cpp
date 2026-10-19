#include "GroupsPage.h"

#include <algorithm>

#include <QAction>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "AthenaStyle.h"
#include "M3List.h"
#include "M3Widgets.h"
#include "MaterialTheme.h"
#include "PackageActions.h"
#include "PackageBrowser.h"

namespace {
constexpr int GroupIdRole = Qt::UserRole;
constexpr int GroupIsMetaRole = Qt::UserRole + 1;

enum StateFilter { All, Installed, NotInstalled };

QLineEdit *searchField(const QString &placeholder, QWidget *parent)
{
    auto *edit = new QLineEdit(parent);
    edit->setPlaceholderText(placeholder);
    edit->setClearButtonEnabled(true);
    edit->addAction(MaterialTheme::icon(QStringLiteral("search"), MaterialTheme::current().onSurfaceVariant, 18),
                    QLineEdit::LeadingPosition);
    return edit;
}
} // namespace

GroupsPage::GroupsPage(PackageBackend *backend, QWidget *parent) : QWidget(parent), m_backend(backend)
{
    // Left: every group, searchable.
    m_statusLabel = M3::label(tr("Loading groups…"), QStringLiteral("muted"), this);
    m_refreshButton = new QPushButton(tr("Refresh"), this);
    MaterialTheme::styleButton(m_refreshButton, QStringLiteral("text"), QStringLiteral("refresh"));
    auto *leftTop = new QHBoxLayout;
    leftTop->addWidget(m_statusLabel, 1);
    leftTop->addWidget(m_refreshButton);
    m_groupSearch = searchField(tr("Search groups"), this);
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    M3List::apply(m_tree, {-1, /*trailing=*/1, /*plain=*/true});

    auto *left = new QWidget(this);
    left->setMinimumWidth(260);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, AthenaStyle::kSidebarGap, 0);
    leftLayout->setSpacing(10);
    leftLayout->addLayout(leftTop);
    leftLayout->addWidget(m_groupSearch);
    leftLayout->addWidget(m_tree, 1);

    // Right: the selected group.
    auto *header = M3::card(QStringLiteral("card"), this);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 16, 20, 16);
    headerLayout->setSpacing(16);
    headerLayout->addWidget(M3::symbolLabel(QStringLiteral("inventory_2"), 22, header, true), 0, Qt::AlignTop);
    auto *headerText = new QVBoxLayout;
    headerText->setSpacing(4);
    m_categoryTitle = M3::label(QString(), QStringLiteral("title"), header);
    m_categoryDescription = M3::label(QString(), QStringLiteral("muted"), header);
    m_categoryDescription->setWordWrap(true);
    m_groupStats = M3::label(QString(), QStringLiteral("label"), header);
    headerText->addWidget(m_categoryTitle);
    headerText->addWidget(m_categoryDescription);
    headerText->addWidget(m_groupStats);
    headerLayout->addLayout(headerText, 1);
    auto *headerActions = new QVBoxLayout;
    headerActions->setSpacing(8);
    m_installMissingButton = new QPushButton(header);
    MaterialTheme::styleButton(m_installMissingButton, QStringLiteral("filled"), QStringLiteral("download"));
    m_removeInstalledButton = new QPushButton(tr("Remove installed…"), header);
    MaterialTheme::styleButton(m_removeInstalledButton, QStringLiteral("text"), QStringLiteral("delete"));
    headerActions->addWidget(m_installMissingButton);
    headerActions->addWidget(m_removeInstalledButton);
    headerActions->addStretch(1);
    headerLayout->addLayout(headerActions);

    auto *filterRow = new QHBoxLayout;
    filterRow->setSpacing(12);
    m_packageSearch = searchField(tr("Search in this group"), this);
    filterRow->addWidget(m_packageSearch, 1);
    m_stateFilter = M3::segmented({{tr("All"), QString()}, {tr("Installed"), QStringLiteral("check")}, {tr("Not installed"), QString()}},
                                  this, filterRow);
    m_stateFilter->button(All)->setChecked(true);

    m_browser = new PackageBrowser(backend, PackageBrowser::Mode::InstallRemove, this);

    auto *detail = new QWidget(this);
    auto *detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(AthenaStyle::kSidebarGap, 0, 0, 0);
    detailLayout->setSpacing(12);
    detailLayout->addWidget(header);
    detailLayout->addLayout(filterRow);
    detailLayout->addWidget(m_browser, 1);

    m_emptyState = M3::label(tr("Choose a group on the left to see its packages."), QStringLiteral("muted"), this);
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setWordWrap(true);
    m_detailStack = new QStackedWidget(this);
    m_detailStack->addWidget(m_emptyState);
    m_detailStack->addWidget(detail);

    auto *splitter = new QSplitter(this);
    AthenaStyle::makeSidebarSplitter(splitter);
    splitter->addWidget(left);
    splitter->addWidget(m_detailStack);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({290, 900});

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(splitter, 1);

    connect(m_refreshButton, &QPushButton::clicked, this, &GroupsPage::refresh);
    connect(m_groupSearch, &QLineEdit::textChanged, this, &GroupsPage::filterGroups);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &GroupsPage::onGroupSelected);
    connect(m_packageSearch, &QLineEdit::textChanged, m_browser, &PackageBrowser::setFilterText);
    connect(m_stateFilter, &QButtonGroup::idClicked, this, [this]() { showPackages(); });
    connect(m_installMissingButton, &QPushButton::clicked, this, &GroupsPage::installMissing);
    connect(m_removeInstalledButton, &QPushButton::clicked, this, &GroupsPage::removeInstalled);
    connect(m_browser, &PackageBrowser::refreshRequested, this, &GroupsPage::refresh);
    connect(&m_listWatcher, &QFutureWatcher<QVector<PackageGroupInfo>>::finished, this, &GroupsPage::onGroupsLoaded);
    connect(&m_contentsWatcher, &QFutureWatcher<GroupContentsResult>::finished, this, &GroupsPage::onGroupContentsLoaded);

    refresh();
}

void GroupsPage::refresh()
{
    if (!m_backend || m_listWatcher.isRunning())
        return;
    m_refreshButton->setEnabled(false);
    m_statusLabel->setText(tr("Loading groups…"));
    PackageBackend *backend = m_backend;
    m_listWatcher.setFuture(QtConcurrent::run([backend]() { return backend->listGroups(); }));
}

void GroupsPage::onGroupsLoaded()
{
    QVector<PackageGroupInfo> groups = m_listWatcher.result();
    std::sort(groups.begin(), groups.end(), [](const PackageGroupInfo &a, const PackageGroupInfo &b) {
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });

    const QString previouslySelectedId =
        m_tree->currentItem() ? m_tree->currentItem()->data(0, GroupIdRole).toString() : QString();

    const QSignalBlocker blocker(m_tree);
    m_tree->clear();

    // dnf has environments (groups of groups) as well as groups; each gets
    // its own section. Other backends only have groups.
    const bool anyMeta = std::any_of(groups.begin(), groups.end(), [](const PackageGroupInfo &g) { return g.isMeta; });
    QTreeWidgetItem *metaRoot = nullptr;
    QTreeWidgetItem *groupRoot = nullptr;
    if (anyMeta) {
        metaRoot = new QTreeWidgetItem(m_tree, {tr("Environments"), QString()});
        M3List::makeHeader(metaRoot);
        groupRoot = new QTreeWidgetItem(m_tree, {tr("Groups"), QString()});
        M3List::makeHeader(groupRoot);
    }

    QTreeWidgetItem *itemToReselect = nullptr;
    int installedCount = 0;
    for (const PackageGroupInfo &group : groups) {
        QTreeWidgetItem *parent = group.isMeta && metaRoot ? metaRoot : groupRoot;
        // Only installed groups say so; "No" on every other row is noise.
        const QStringList columns = {group.name, group.installed ? tr("Installed") : QString()};
        auto *item = parent ? new QTreeWidgetItem(parent, columns) : new QTreeWidgetItem(m_tree, columns);
        item->setData(0, GroupIdRole, group.id);
        item->setData(0, GroupIsMetaRole, group.isMeta);
        if (!group.description.isEmpty())
            item->setToolTip(0, group.description);
        if (group.installed)
            ++installedCount;
        if (!previouslySelectedId.isEmpty() && group.id == previouslySelectedId)
            itemToReselect = item;
    }
    m_tree->expandAll();
    filterGroups(m_groupSearch->text());

    m_statusLabel->setText(installedCount > 0 ? tr("%1 groups · %2 installed").arg(groups.size()).arg(installedCount)
                                              : tr("%n group(s)", "", groups.size()));
    m_refreshButton->setEnabled(true);

    if (groups.isEmpty()) {
        m_emptyState->setText(tr("Your package manager doesn't report any groups."));
        m_detailStack->setCurrentIndex(0);
        return;
    }
    if (itemToReselect) {
        m_tree->setCurrentItem(itemToReselect);
        onGroupSelected(itemToReselect, nullptr); // reload its contents (signals were blocked)
    }
}

void GroupsPage::filterGroups(const QString &text)
{
    const QString needle = text.trimmed();
    auto matches = [&needle](QTreeWidgetItem *item) {
        return needle.isEmpty() || item->text(0).contains(needle, Qt::CaseInsensitive)
            || item->toolTip(0).contains(needle, Qt::CaseInsensitive);
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = m_tree->topLevelItem(i);
        if (top->data(0, M3List::HeaderRole).toBool()) {
            int visible = 0;
            for (int c = 0; c < top->childCount(); ++c) {
                const bool show = matches(top->child(c));
                top->child(c)->setHidden(!show);
                visible += show ? 1 : 0;
            }
            top->setHidden(visible == 0);
        } else {
            top->setHidden(!matches(top));
        }
    }
}

void GroupsPage::onGroupSelected(QTreeWidgetItem *current, QTreeWidgetItem *previous)
{
    Q_UNUSED(previous);
    if (!current || !m_backend)
        return;
    const QVariant idData = current->data(0, GroupIdRole);
    if (!idData.isValid())
        return; // a section header

    const QString groupId = idData.toString();
    const bool isMeta = current->data(0, GroupIsMetaRole).toBool();
    m_detailStack->setCurrentIndex(1);
    m_categoryTitle->setText(current->text(0));
    m_categoryDescription->setText(tr("Loading…"));
    m_groupStats->clear();
    m_installMissingButton->setEnabled(false);
    m_removeInstalledButton->setEnabled(false);
    m_browser->setBusy(true);

    PackageBackend *backend = m_backend;
    m_contentsWatcher.setFuture(QtConcurrent::run([backend, groupId, isMeta]() {
        GroupContentsResult result;
        const PackageGroupInfo group = backend->groupDetails(groupId, isMeta);
        result.description = group.description;

        QStringList packageNames;
        if (!group.subGroups.isEmpty()) {
            // A meta-group's contents are other groups; flatten each
            // sub-group's packages into one combined list.
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
    }));
}

void GroupsPage::onGroupContentsLoaded()
{
    const GroupContentsResult result = m_contentsWatcher.result();
    m_packages = result.packages;
    m_categoryDescription->setText(result.description.isEmpty() ? tr("No description.") : result.description);

    const int installed = int(std::count_if(m_packages.begin(), m_packages.end(), [](const PackageInfo &p) { return p.installed; }));
    const int missing = int(m_packages.size()) - installed;
    m_groupStats->setText(m_packages.isEmpty() ? tr("No packages")
                                               : tr("%1 of %n package(s) installed", "", m_packages.size()).arg(installed));
    m_installMissingButton->setText(missing == int(m_packages.size()) ? tr("Install group")
                                                                     : tr("Install %n missing", "", missing));
    m_installMissingButton->setEnabled(missing > 0);
    m_removeInstalledButton->setEnabled(installed > 0);
    showPackages();
    m_browser->setBusy(false);
}

void GroupsPage::showPackages()
{
    const int filter = m_stateFilter->checkedId();
    QVector<PackageInfo> shown;
    for (const PackageInfo &package : m_packages) {
        if (filter == All || (filter == Installed) == package.installed)
            shown.append(package);
    }
    m_browser->setPackages(shown);
    m_browser->setFilterText(m_packageSearch->text());
}

void GroupsPage::installMissing()
{
    QStringList names;
    for (const PackageInfo &package : m_packages) {
        if (!package.installed)
            names.append(package.name);
    }
    // refresh() reselects the group, which reloads its packages too.
    PackageActions::installPackages(this, m_backend, names, [this](bool) { refresh(); });
}

void GroupsPage::removeInstalled()
{
    QStringList names;
    for (const PackageInfo &package : m_packages) {
        if (package.installed)
            names.append(package.name);
    }
    // The confirmation shows the resolved plan (what else goes with them)
    // before anything is removed.
    PackageActions::removePackages(this, m_backend, names, [this](bool) { refresh(); });
}
