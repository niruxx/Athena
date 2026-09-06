#include "LeftoverDataPage.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "../core/DirSizeScanner.h"
#include "AppIcons.h"
#include "PackageActions.h"

namespace {
constexpr int RolePath = Qt::UserRole;
constexpr int RoleIsDir = Qt::UserRole + 1;
constexpr int RoleSize = Qt::UserRole + 2;

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
} // namespace

LeftoverDataPage::LeftoverDataPage(FlatpakBackend *backend, QWidget *parent) : QWidget(parent), m_backend(backend)
{
    auto *info = new QLabel(tr("Data left behind under <code>~/.var/app/</code> by applications that "
                                "are no longer installed. Safe to delete once you're sure you won't "
                                "reinstall the app and want its old settings back."),
                             this);
    info->setWordWrap(true);

    m_includeOverridesCheck = new QCheckBox(
        tr("Also list orphaned permission overrides (~/.local/share/flatpak/overrides)"), this);
    m_includeOverridesCheck->setChecked(true);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("Application ID"), tr("Size"), tr("Location")});
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);

    m_refreshButton = new QPushButton(AppIcons::refresh(), tr("Refresh"), this);
    m_openButton = new QPushButton(AppIcons::folder(), tr("Open Folder"), this);
    m_deleteButton = new QPushButton(AppIcons::uninstall(), tr("Delete Selected..."), this);
    m_deleteAllButton = new QPushButton(AppIcons::clean(), tr("Delete All Leftovers..."), this);
    m_openButton->setEnabled(false);
    m_deleteButton->setEnabled(false);
    m_deleteAllButton->setEnabled(false);
    m_totalLabel = new QLabel(this);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->addWidget(m_refreshButton);
    buttonRow->addWidget(m_totalLabel);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_openButton);
    buttonRow->addWidget(m_deleteButton);
    buttonRow->addWidget(m_deleteAllButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(info);
    layout->addWidget(m_includeOverridesCheck);
    layout->addWidget(m_tree, 1);
    layout->addLayout(buttonRow);

    connect(m_refreshButton, &QPushButton::clicked, this, &LeftoverDataPage::refresh);
    connect(m_includeOverridesCheck, &QCheckBox::toggled, this, &LeftoverDataPage::refresh);
    connect(m_openButton, &QPushButton::clicked, this, &LeftoverDataPage::onOpenFolder);
    connect(m_deleteButton, &QPushButton::clicked, this, &LeftoverDataPage::onDeleteSelected);
    connect(m_deleteAllButton, &QPushButton::clicked, this, &LeftoverDataPage::onDeleteAll);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &LeftoverDataPage::onSelectionChanged);
    connect(&m_appListWatcher, &QFutureWatcher<QVector<PackageInfo>>::finished, this,
            &LeftoverDataPage::onAppListLoaded);

    refresh();
}

void LeftoverDataPage::refresh()
{
    if (!m_backend)
        return;

    m_refreshButton->setEnabled(false);
    m_totalLabel->setText(tr("Loading..."));

    FlatpakBackend *backend = m_backend;
    QFuture<QVector<PackageInfo>> future = QtConcurrent::run([backend]() { return backend->listInstalled(); });
    m_appListWatcher.setFuture(future);
}

void LeftoverDataPage::onAppListLoaded()
{
    m_tree->clear();
    m_totalLabel->clear();
    m_refreshButton->setEnabled(true);

    const QVector<PackageInfo> apps = m_appListWatcher.result();
    QSet<QString> installed;
    for (const PackageInfo &app : apps)
        installed.insert(app.name);

    QStringList dataPaths;
    const QDir dataRoot(FlatpakBackend::userDataRoot());
    if (dataRoot.exists()) {
        const QStringList dirs = dataRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &dirName : dirs) {
            if (installed.contains(dirName))
                continue;
            const QString path = dataRoot.filePath(dirName);
            auto *item = new QTreeWidgetItem(m_tree);
            item->setText(0, dirName);
            item->setText(1, tr("scanning..."));
            item->setText(2, path);
            item->setData(0, RolePath, path);
            item->setData(0, RoleIsDir, true);
            dataPaths << path;
        }
    }

    if (m_includeOverridesCheck->isChecked()) {
        const QDir overridesRoot(FlatpakBackend::userOverridesRoot());
        if (overridesRoot.exists()) {
            const QStringList files = overridesRoot.entryList(QDir::Files, QDir::Name);
            for (const QString &fileName : files) {
                if (installed.contains(fileName))
                    continue;
                const QString path = overridesRoot.filePath(fileName);
                const qint64 size = QFileInfo(path).size();
                auto *item = new QTreeWidgetItem(m_tree);
                item->setText(0, tr("%1 (override only)").arg(fileName));
                item->setText(1, formatSize(size));
                item->setText(2, path);
                item->setData(0, RolePath, path);
                item->setData(0, RoleIsDir, false);
                item->setData(0, RoleSize, size);
            }
        }
    }

    if (m_scanner) {
        m_scanner->wait();
        m_scanner->deleteLater();
        m_scanner = nullptr;
    }
    if (!dataPaths.isEmpty()) {
        m_scanner = new DirSizeScanner(dataPaths, this);
        connect(m_scanner, &DirSizeScanner::sizeComputed, this, &LeftoverDataPage::onSizeComputed);
        connect(m_scanner, &DirSizeScanner::finished, this, &LeftoverDataPage::onScanFinished);
        m_scanner->start();
    } else {
        onScanFinished();
    }

    m_deleteAllButton->setEnabled(m_tree->topLevelItemCount() > 0);
}

void LeftoverDataPage::onSizeComputed(const QString &path, qint64 bytes)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (item->data(0, RolePath).toString() == path) {
            item->setText(1, formatSize(bytes));
            item->setData(0, RoleSize, bytes);
            break;
        }
    }
}

void LeftoverDataPage::onScanFinished()
{
    qint64 total = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        total += m_tree->topLevelItem(i)->data(0, RoleSize).toLongLong();
    m_totalLabel->setText(m_tree->topLevelItemCount() == 0
                               ? tr("No leftover data found.")
                               : tr("%n item(s), total %1", nullptr, m_tree->topLevelItemCount())
                                     .arg(formatSize(total)));
}

void LeftoverDataPage::onSelectionChanged()
{
    const auto items = m_tree->selectedItems();
    m_openButton->setEnabled(items.size() == 1);
    m_deleteButton->setEnabled(!items.isEmpty());
}

void LeftoverDataPage::onOpenFolder()
{
    const auto items = m_tree->selectedItems();
    if (items.isEmpty())
        return;
    QString path = items.first()->data(0, RolePath).toString();
    if (!items.first()->data(0, RoleIsDir).toBool())
        path = QFileInfo(path).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void LeftoverDataPage::deletePaths(const QStringList &paths)
{
    // Path -> isDir, captured before handing off to the worker thread
    // since QTreeWidgetItem must only be touched from the UI thread.
    QMap<QString, bool> isDirByPath;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        const QString path = item->data(0, RolePath).toString();
        if (paths.contains(path))
            isDirByPath.insert(path, item->data(0, RoleIsDir).toBool());
    }

    PackageActions::confirmAndRun(
        this, tr("Delete Leftover Data"), tr("Permanently delete %n leftover item(s)?", nullptr, paths.size()),
        [isDirByPath]() -> OperationResult {
            OperationResult op;
            op.success = true;
            for (auto it = isDirByPath.constBegin(); it != isDirByPath.constEnd(); ++it) {
                const bool ok = it.value() ? QDir(it.key()).removeRecursively() : QFile::remove(it.key());
                if (!ok) {
                    op.success = false;
                    op.output += QStringLiteral("Could not remove %1\n").arg(it.key());
                }
            }
            return op;
        },
        [this](bool) { refresh(); }, /*requiresPrivileges=*/false);
}

void LeftoverDataPage::onDeleteSelected()
{
    const auto items = m_tree->selectedItems();
    if (items.isEmpty())
        return;
    QStringList paths;
    for (auto *item : items)
        paths << item->data(0, RolePath).toString();
    deletePaths(paths);
}

void LeftoverDataPage::onDeleteAll()
{
    if (m_tree->topLevelItemCount() == 0)
        return;
    QStringList paths;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        paths << m_tree->topLevelItem(i)->data(0, RolePath).toString();
    deletePaths(paths);
}
