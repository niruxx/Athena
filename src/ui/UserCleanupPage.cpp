#include "UserCleanupPage.h"

#include <QDir>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "../core/UserCleanupUtils.h"
#include "AppIcons.h"
#include "PackageActions.h"

namespace {
constexpr int RoleRow = Qt::UserRole;

QString formatSize(qint64 bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', unit == 0 ? 0 : 1).arg(units[unit]);
}

// A single path is shown in full; more than one (e.g. Shell & Tool
// History, scattered across several dotfiles) is summarized instead of
// spelling out every one, which would otherwise dominate the row/column.
QString locationSummary(const QStringList &paths)
{
    if (paths.size() == 1)
        return paths.first();
    return QObject::tr("%1 (+%n more)", "", paths.size() - 1).arg(paths.first());
}

QString locationDetail(const QStringList &paths)
{
    return paths.join(QChar('\n'));
}
} // namespace

namespace UserCleanupTargets {

QVector<CleanupTarget> general()
{
    const QString home = QDir::homePath();
    return {
        {QObject::tr("Temporary Files"), AppIcons::temporary(), {QStringLiteral("/tmp")},
         QObject::tr("Files you own under the system-wide /tmp directory. Other users' files there are "
                     "left untouched.")},
        {QObject::tr("Cache"), AppIcons::cache(), {home + QStringLiteral("/.cache")},
         QObject::tr("Cached application data. Safe to clear — it will be regenerated as needed.")},
    };
}

QVector<CleanupTarget> advanced()
{
    const QString home = QDir::homePath();
    return {
        {QObject::tr("Trash"), AppIcons::trash(), {home + QStringLiteral("/.local/share/Trash")},
         QObject::tr("Files and folders deleted through a file manager but not yet emptied.")},
        {QObject::tr("Thumbnail Cache"), AppIcons::image(), {home + QStringLiteral("/.cache/thumbnails")},
         QObject::tr("Cached preview images for pictures and videos.")},
        {QObject::tr("Shell & Tool History"), AppIcons::terminal(),
         {home + QStringLiteral("/.bash_history"), home + QStringLiteral("/.zsh_history"),
          home + QStringLiteral("/.local/share/fish/fish_history"), home + QStringLiteral("/.python_history"),
          home + QStringLiteral("/.lesshst"), home + QStringLiteral("/.viminfo"),
          home + QStringLiteral("/.node_repl_history")},
         QObject::tr("Command history for common shells and interactive tools.")},
        {QObject::tr("Recently Used Files List"), AppIcons::history(),
         {home + QStringLiteral("/.local/share/recently-used.xbel")},
         QObject::tr("The list of recently opened files shown by file-picker dialogs and file managers.")},
    };
}

} // namespace UserCleanupTargets

UserCleanupPage::UserCleanupPage(const QString &introText, const QVector<CleanupTarget> &targets, QWidget *parent)
    : QWidget(parent), m_targets(targets)
{
    auto *introLabel = new QLabel(introText, this);
    introLabel->setWordWrap(true);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("Item"), tr("Size"), tr("Location")});
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setIconSize(QSize(20, 20));
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);

    for (int i = 0; i < m_targets.size(); ++i) {
        const CleanupTarget &target = m_targets.at(i);
        auto *item = new QTreeWidgetItem(m_tree);
        item->setIcon(0, target.icon);
        item->setText(0, target.label);
        item->setText(1, tr("scanning..."));
        item->setText(2, locationSummary(target.paths));
        item->setToolTip(2, locationDetail(target.paths));
        item->setData(0, RoleRow, i);
    }

    // Details panel: the tree's Location column only ever shows a short
    // summary (a long path, or "+N more" for multi-path targets like Shell
    // & Tool History), so the full description and every path a target
    // covers is shown here instead, for whichever row is currently
    // focused.
    m_detailsFrame = new QFrame(this);
    m_detailsFrame->setFrameShape(QFrame::StyledPanel);
    auto *detailsLayout = new QHBoxLayout(m_detailsFrame);

    m_detailsIcon = new QLabel(m_detailsFrame);
    m_detailsIcon->setFixedSize(32, 32);
    m_detailsIcon->setScaledContents(true);
    detailsLayout->addWidget(m_detailsIcon, 0, Qt::AlignTop);

    auto *detailsTextLayout = new QVBoxLayout;
    m_detailsTitle = new QLabel(m_detailsFrame);
    QFont titleFont = m_detailsTitle->font();
    titleFont.setBold(true);
    m_detailsTitle->setFont(titleFont);
    m_detailsDescription = new QLabel(m_detailsFrame);
    m_detailsDescription->setWordWrap(true);
    m_detailsLocation = new QLabel(m_detailsFrame);
    m_detailsLocation->setWordWrap(true);
    m_detailsLocation->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont monoFont = m_detailsLocation->font();
    monoFont.setFamily(QStringLiteral("monospace"));
    m_detailsLocation->setFont(monoFont);
    detailsTextLayout->addWidget(m_detailsTitle);
    detailsTextLayout->addWidget(m_detailsDescription);
    detailsTextLayout->addWidget(m_detailsLocation);
    detailsLayout->addLayout(detailsTextLayout, 1);

    m_refreshButton = new QPushButton(AppIcons::refresh(), tr("Refresh"), this);
    m_deleteButton = new QPushButton(AppIcons::uninstall(), tr("Delete Selected..."), this);
    m_deleteAllButton = new QPushButton(AppIcons::clean(), tr("Delete All..."), this);
    m_deleteButton->setEnabled(false);
    m_totalLabel = new QLabel(this);
    QFont totalFont = m_totalLabel->font();
    totalFont.setBold(true);
    m_totalLabel->setFont(totalFont);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->addWidget(m_refreshButton);
    buttonRow->addWidget(m_totalLabel);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_deleteButton);
    buttonRow->addWidget(m_deleteAllButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(introLabel);
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_detailsFrame);
    layout->addLayout(buttonRow);

    connect(m_refreshButton, &QPushButton::clicked, this, &UserCleanupPage::refresh);
    connect(m_deleteButton, &QPushButton::clicked, this, &UserCleanupPage::onDeleteSelected);
    connect(m_deleteAllButton, &QPushButton::clicked, this, &UserCleanupPage::onDeleteAll);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &UserCleanupPage::onSelectionChanged);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &UserCleanupPage::updateDetailsPanel);
    connect(&m_sizeWatcher, &QFutureWatcher<QVector<qint64>>::finished, this, &UserCleanupPage::onSizesComputed);

    if (m_tree->topLevelItemCount() > 0)
        m_tree->setCurrentItem(m_tree->topLevelItem(0));

    refresh();
}

void UserCleanupPage::refresh()
{
    m_refreshButton->setEnabled(false);
    m_deleteButton->setEnabled(false);
    m_deleteAllButton->setEnabled(false);
    m_totalLabel->setText(tr("Scanning..."));

    const QVector<CleanupTarget> targets = m_targets;
    QFuture<QVector<qint64>> future = QtConcurrent::run([targets]() {
        QVector<qint64> sizes;
        sizes.reserve(targets.size());
        for (const CleanupTarget &target : targets) {
            qint64 total = 0;
            for (const QString &path : target.paths)
                total += UserCleanupUtils::ownedSize(path);
            sizes << total;
        }
        return sizes;
    });
    m_sizeWatcher.setFuture(future);
}

void UserCleanupPage::onSizesComputed()
{
    const QVector<qint64> sizes = m_sizeWatcher.result();
    qint64 total = 0;
    for (int i = 0; i < m_tree->topLevelItemCount() && i < sizes.size(); ++i) {
        m_tree->topLevelItem(i)->setText(1, formatSize(sizes.at(i)));
        total += sizes.at(i);
    }

    m_refreshButton->setEnabled(true);
    m_deleteAllButton->setEnabled(!m_targets.isEmpty());
    onSelectionChanged();
    m_totalLabel->setText(tr("Total: %1").arg(formatSize(total)));
}

void UserCleanupPage::onSelectionChanged()
{
    m_deleteButton->setEnabled(!m_tree->selectedItems().isEmpty());
}

void UserCleanupPage::updateDetailsPanel()
{
    const QTreeWidgetItem *item = m_tree->currentItem();
    if (!item) {
        m_detailsIcon->clear();
        m_detailsTitle->clear();
        m_detailsDescription->clear();
        m_detailsLocation->clear();
        return;
    }

    const int row = item->data(0, RoleRow).toInt();
    const CleanupTarget &target = m_targets.at(row);
    m_detailsIcon->setPixmap(target.icon.pixmap(32, 32));
    m_detailsTitle->setText(target.label);
    m_detailsDescription->setText(target.description);
    m_detailsLocation->setText(locationDetail(target.paths));
}

void UserCleanupPage::deleteTargets(const QVector<int> &rows)
{
    if (rows.isEmpty())
        return;

    QVector<CleanupTarget> selected;
    for (int row : rows)
        selected << m_targets.at(row);

    PackageActions::confirmAndRun(
        this, tr("Clear Data"), tr("Permanently delete the data for %n selected item(s)?", nullptr, rows.size()),
        [selected]() -> OperationResult {
            OperationResult op;
            op.success = true;
            for (const CleanupTarget &target : selected) {
                for (const QString &path : target.paths) {
                    if (!UserCleanupUtils::clearOwnedPath(path)) {
                        op.success = false;
                        op.output += QStringLiteral("Could not fully clear %1\n").arg(path);
                    }
                }
            }
            return op;
        },
        [this](bool) { refresh(); }, /*requiresPrivileges=*/false);
}

void UserCleanupPage::onDeleteSelected()
{
    QVector<int> rows;
    for (auto *item : m_tree->selectedItems())
        rows << item->data(0, RoleRow).toInt();
    deleteTargets(rows);
}

void UserCleanupPage::onDeleteAll()
{
    QVector<int> rows;
    for (int i = 0; i < m_targets.size(); ++i)
        rows << i;
    deleteTargets(rows);
}
