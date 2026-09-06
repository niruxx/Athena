#include "UserDataPage.h"

#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "../core/DirSizeScanner.h"
#include "AppIcons.h"
#include "PackageActions.h"

namespace {
constexpr int RolePath = Qt::UserRole;

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

UserDataPage::UserDataPage(FlatpakBackend *backend, QWidget *parent) : QWidget(parent), m_backend(backend)
{
    auto *info = new QLabel(tr("Data stored under <code>~/.var/app/</code> for currently installed "
                                "applications. Clearing data removes settings, caches, and saved "
                                "files for that app without uninstalling it."),
                             this);
    info->setWordWrap(true);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("Application"), tr("Size"), tr("Location")});
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);

    m_refreshButton = new QPushButton(AppIcons::refresh(), tr("Refresh"), this);
    m_openButton = new QPushButton(AppIcons::folder(), tr("Open Folder"), this);
    m_clearButton = new QPushButton(AppIcons::uninstall(), tr("Clear Data..."), this);
    m_openButton->setEnabled(false);
    m_clearButton->setEnabled(false);
    m_totalLabel = new QLabel(this);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->addWidget(m_refreshButton);
    buttonRow->addWidget(m_totalLabel);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_openButton);
    buttonRow->addWidget(m_clearButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(info);
    layout->addWidget(m_tree, 1);
    layout->addLayout(buttonRow);

    connect(m_refreshButton, &QPushButton::clicked, this, &UserDataPage::refresh);
    connect(m_openButton, &QPushButton::clicked, this, &UserDataPage::onOpenFolder);
    connect(m_clearButton, &QPushButton::clicked, this, &UserDataPage::onClearData);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &UserDataPage::onSelectionChanged);
    connect(&m_appListWatcher, &QFutureWatcher<QVector<PackageInfo>>::finished, this,
            &UserDataPage::onAppListLoaded);

    refresh();
}

void UserDataPage::refresh()
{
    if (!m_backend)
        return;

    m_refreshButton->setEnabled(false);
    m_totalLabel->setText(tr("Loading..."));

    FlatpakBackend *backend = m_backend;
    QFuture<QVector<PackageInfo>> future = QtConcurrent::run([backend]() { return backend->listInstalled(); });
    m_appListWatcher.setFuture(future);
}

void UserDataPage::onAppListLoaded()
{
    m_tree->clear();
    m_totalLabel->clear();
    m_refreshButton->setEnabled(true);

    const QVector<PackageInfo> apps = m_appListWatcher.result();
    QHash<QString, QString> prettyNames;
    for (const PackageInfo &app : apps)
        prettyNames.insert(app.name, app.description.section(QStringLiteral(" — "), 0, 0));

    const QDir root(FlatpakBackend::userDataRoot());
    if (!root.exists()) {
        m_totalLabel->setText(tr("No Flatpak user data found."));
        return;
    }

    const QStringList dirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    QStringList paths;
    for (const QString &dirName : dirs) {
        if (!prettyNames.contains(dirName))
            continue; // only currently-installed apps here; see Leftover Data otherwise
        const QString prettyName = prettyNames.value(dirName);
        const QString path = root.filePath(dirName);
        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, prettyName.isEmpty() ? dirName : tr("%1 (%2)").arg(prettyName, dirName));
        item->setText(1, tr("scanning..."));
        item->setText(2, path);
        item->setData(0, RolePath, path);
        paths << path;
    }

    if (m_scanner) {
        m_scanner->wait();
        m_scanner->deleteLater();
        m_scanner = nullptr;
    }
    if (paths.isEmpty()) {
        m_totalLabel->setText(tr("No data directories for installed applications."));
        return;
    }
    m_scanner = new DirSizeScanner(paths, this);
    connect(m_scanner, &DirSizeScanner::sizeComputed, this, &UserDataPage::onSizeComputed);
    connect(m_scanner, &DirSizeScanner::finished, this, &UserDataPage::onScanFinished);
    m_scanner->start();
}

void UserDataPage::onSizeComputed(const QString &path, qint64 bytes)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (item->data(0, RolePath).toString() == path) {
            item->setText(1, formatSize(bytes));
            item->setData(1, Qt::UserRole, bytes);
            break;
        }
    }
}

void UserDataPage::onScanFinished()
{
    qint64 total = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        total += m_tree->topLevelItem(i)->data(1, Qt::UserRole).toLongLong();
    m_totalLabel->setText(tr("Total: %1").arg(formatSize(total)));
}

void UserDataPage::onSelectionChanged()
{
    const auto items = m_tree->selectedItems();
    m_openButton->setEnabled(items.size() == 1);
    m_clearButton->setEnabled(!items.isEmpty());
}

void UserDataPage::onOpenFolder()
{
    const auto items = m_tree->selectedItems();
    if (items.isEmpty())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(items.first()->data(0, RolePath).toString()));
}

void UserDataPage::onClearData()
{
    const auto items = m_tree->selectedItems();
    if (items.isEmpty())
        return;

    QStringList names;
    QStringList paths;
    for (auto *item : items) {
        names << item->text(0);
        paths << item->data(0, RolePath).toString();
    }

    PackageActions::confirmAndRun(
        this, tr("Clear Application Data"),
        tr("Delete all user data for:\n\n%1\n\nThis removes settings, caches, and saved files for "
           "these applications, but keeps them installed. This cannot be undone.")
            .arg(names.join('\n')),
        [paths]() -> OperationResult {
            OperationResult op;
            op.success = true;
            for (const QString &path : paths) {
                if (!QDir(path).removeRecursively()) {
                    op.success = false;
                    op.output += QStringLiteral("Could not fully clear data at %1\n").arg(path);
                }
            }
            return op;
        },
        [this](bool) { refresh(); }, /*requiresPrivileges=*/false);
}
