#include "UserBackupRestorePage.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "../core/DirSizeScanner.h"
#include "../core/ProcessRunner.h"
#include "AppIcons.h"
#include "PackageActions.h"

using ProcessRunner::Result;

namespace {
constexpr int RoleSize = Qt::UserRole;

// Hidden top-level entries under $HOME that are transient/regenerable
// rather than actual configuration — unchecked by default so a routine
// backup doesn't balloon in size, though the user can still opt in.
const QSet<QString> kDefaultUncheckedEntries = {QStringLiteral(".cache")};

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

// Top-level entry names found in a backup, for the Restore section's
// preview list — either the immediate children of a folder-mode backup,
// or the top segment of every path an archive-mode backup's tar members
// start with (a member like ".config/foo/bar" only contributes ".config").
QStringList topLevelEntries(const QString &source, bool isFolder)
{
    if (isFolder) {
        return QDir(source).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                                       QDir::Name);
    }

    const Result result = ProcessRunner::run("tar", {"-tzf", source}, 30000);
    if (!(result.started && result.exitCode == 0))
        return {};

    QStringList entries;
    QSet<QString> seen;
    const QStringList lines = result.stdOut.split(QChar('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString top = line.section(QChar('/'), 0, 0);
        if (!seen.contains(top)) {
            seen.insert(top);
            entries << top;
        }
    }
    return entries;
}
} // namespace

UserBackupRestorePage::UserBackupRestorePage(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);

    auto *backupGroup = new QGroupBox(tr("Backup"), this);
    auto *backupLayout = new QVBoxLayout(backupGroup);
    backupLayout->addWidget(new QLabel(tr("Save your dotfiles and configuration — hidden files and folders "
                                           "directly under your home directory — to a destination you choose, "
                                           "either as a single compressed archive or a plain uncompressed copy."),
                                        backupGroup));

    auto *destRow = new QHBoxLayout;
    m_backupDestEdit = new QLineEdit(backupGroup);
    m_backupDestEdit->setPlaceholderText(tr("Destination folder..."));
    auto *browseDestButton = new QPushButton(AppIcons::folder(), tr("Browse..."), backupGroup);
    destRow->addWidget(m_backupDestEdit, 1);
    destRow->addWidget(browseDestButton);
    backupLayout->addLayout(destRow);

    auto *formatRow = new QHBoxLayout;
    m_compressedRadio = new QRadioButton(tr("Compressed archive (.tar.gz)"), backupGroup);
    m_copyRadio = new QRadioButton(tr("Uncompressed copy (folder)"), backupGroup);
    m_compressedRadio->setChecked(true);
    formatRow->addWidget(m_compressedRadio);
    formatRow->addWidget(m_copyRadio);
    formatRow->addStretch(1);
    backupLayout->addLayout(formatRow);

    // Two-column body: the entry list gets most of the width, with the
    // list-related controls and the summary/backup action beside it
    // instead of stretched across a mostly-empty row underneath.
    auto *backupBody = new QHBoxLayout;

    m_entriesTree = new QTreeWidget(backupGroup);
    m_entriesTree->setColumnCount(2);
    m_entriesTree->setHeaderLabels({tr("Dotfile / Folder"), tr("Size")});
    m_entriesTree->setRootIsDecorated(false);
    m_entriesTree->setAlternatingRowColors(true);
    m_entriesTree->setSelectionMode(QAbstractItemView::NoSelection);
    m_entriesTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_entriesTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    backupBody->addWidget(m_entriesTree, 1);

    auto *backupSideLayout = new QVBoxLayout;
    m_selectAllCheck = new QCheckBox(tr("Select All"), backupGroup);
    m_selectAllCheck->setChecked(true);
    m_rescanButton = new QPushButton(AppIcons::refresh(), tr("Rescan"), backupGroup);
    m_selectedSummaryLabel = new QLabel(backupGroup);
    m_selectedSummaryLabel->setWordWrap(true);
    QFont summaryFont = m_selectedSummaryLabel->font();
    summaryFont.setBold(true);
    m_selectedSummaryLabel->setFont(summaryFont);
    m_backupButton = new QPushButton(AppIcons::archive(), tr("Back Up Now"), backupGroup);
    backupSideLayout->addWidget(m_selectAllCheck);
    backupSideLayout->addWidget(m_rescanButton);
    backupSideLayout->addSpacing(8);
    backupSideLayout->addWidget(m_selectedSummaryLabel);
    backupSideLayout->addStretch(1);
    backupSideLayout->addWidget(m_backupButton);
    auto *backupSideWidget = new QWidget(backupGroup);
    backupSideWidget->setLayout(backupSideLayout);
    backupSideWidget->setMaximumWidth(220);
    backupBody->addWidget(backupSideWidget);

    backupLayout->addLayout(backupBody, 1);

    outer->addWidget(backupGroup, 3);

    auto *restoreGroup = new QGroupBox(tr("Restore"), this);
    auto *restoreLayout = new QVBoxLayout(restoreGroup);
    restoreLayout->addWidget(new QLabel(tr("Restore dotfiles/configuration from a backup — either kind — "
                                            "back into your home directory, overwriting any existing files "
                                            "with the same name. Handy when moving to a new machine or a "
                                            "fresh OS install."),
                                         restoreGroup));

    auto *sourceRow = new QHBoxLayout;
    m_restoreSourceEdit = new QLineEdit(restoreGroup);
    m_restoreSourceEdit->setPlaceholderText(tr("Backup archive or folder..."));
    auto *browseArchiveButton = new QPushButton(AppIcons::archive(), tr("Browse Archive..."), restoreGroup);
    auto *browseFolderButton = new QPushButton(AppIcons::folder(), tr("Browse Folder..."), restoreGroup);
    sourceRow->addWidget(m_restoreSourceEdit, 1);
    sourceRow->addWidget(browseArchiveButton);
    sourceRow->addWidget(browseFolderButton);
    restoreLayout->addLayout(sourceRow);

    auto *restoreBody = new QHBoxLayout;

    auto *previewLayout = new QVBoxLayout;
    m_previewSummaryLabel = new QLabel(tr("Choose a backup above to see what it contains."), restoreGroup);
    m_previewSummaryLabel->setWordWrap(true);
    m_previewList = new QListWidget(restoreGroup);
    m_previewList->setSelectionMode(QAbstractItemView::NoSelection);
    m_previewList->setAlternatingRowColors(true);
    previewLayout->addWidget(m_previewSummaryLabel);
    previewLayout->addWidget(m_previewList, 1);
    restoreBody->addLayout(previewLayout, 1);

    auto *restoreSideLayout = new QVBoxLayout;
    auto *restoreWarning = new QLabel(
        tr("Files already in your home directory with the same name will be overwritten."), restoreGroup);
    restoreWarning->setWordWrap(true);
    m_restoreButton = new QPushButton(AppIcons::download(), tr("Restore Now..."), restoreGroup);
    restoreSideLayout->addWidget(restoreWarning);
    restoreSideLayout->addStretch(1);
    restoreSideLayout->addWidget(m_restoreButton);
    auto *restoreSideWidget = new QWidget(restoreGroup);
    restoreSideWidget->setLayout(restoreSideLayout);
    restoreSideWidget->setMaximumWidth(220);
    restoreBody->addWidget(restoreSideWidget);

    restoreLayout->addLayout(restoreBody, 1);

    outer->addWidget(restoreGroup, 2);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    outer->addWidget(m_statusLabel);

    connect(browseDestButton, &QPushButton::clicked, this, &UserBackupRestorePage::onBrowseBackupDestination);
    connect(m_rescanButton, &QPushButton::clicked, this, &UserBackupRestorePage::onRescanEntries);
    connect(m_selectAllCheck, &QCheckBox::toggled, this, &UserBackupRestorePage::onSelectAll);
    connect(m_backupButton, &QPushButton::clicked, this, &UserBackupRestorePage::onCreateBackup);
    connect(browseArchiveButton, &QPushButton::clicked, this, &UserBackupRestorePage::onBrowseRestoreArchive);
    connect(browseFolderButton, &QPushButton::clicked, this, &UserBackupRestorePage::onBrowseRestoreFolder);
    connect(m_restoreSourceEdit, &QLineEdit::editingFinished, this, &UserBackupRestorePage::onRestoreSourceEdited);
    connect(m_restoreButton, &QPushButton::clicked, this, &UserBackupRestorePage::onRestore);
    connect(m_entriesTree, &QTreeWidget::itemChanged, this, &UserBackupRestorePage::onEntryChecked);
    connect(&m_scanWatcher, &QFutureWatcher<QStringList>::finished, this, &UserBackupRestorePage::onEntriesScanned);
    connect(&m_backupWatcher, &QFutureWatcher<OperationResult>::finished, this,
            &UserBackupRestorePage::onBackupFinished);
    connect(&m_previewWatcher, &QFutureWatcher<QStringList>::finished, this,
            &UserBackupRestorePage::onPreviewScanned);

    onRescanEntries();
}

void UserBackupRestorePage::onBrowseBackupDestination()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose Backup Destination"), QDir::homePath());
    if (!dir.isEmpty())
        m_backupDestEdit->setText(dir);
}

void UserBackupRestorePage::onRescanEntries()
{
    m_rescanButton->setEnabled(false);
    m_backupButton->setEnabled(false);
    m_selectedSummaryLabel->setText(tr("Scanning..."));

    QFuture<QStringList> future = QtConcurrent::run([]() {
        QStringList dotfiles;
        const QDir home(QDir::homePath());
        const QStringList entries =
            home.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &entry : entries) {
            if (entry.startsWith(QLatin1Char('.')))
                dotfiles << entry;
        }
        return dotfiles;
    });
    m_scanWatcher.setFuture(future);
}

void UserBackupRestorePage::onEntriesScanned()
{
    m_entriesTree->clear();
    const QStringList entries = m_scanWatcher.result();

    QStringList paths;
    for (const QString &entry : entries) {
        auto *item = new QTreeWidgetItem(m_entriesTree);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setText(0, entry);
        item->setCheckState(0, kDefaultUncheckedEntries.contains(entry) ? Qt::Unchecked : Qt::Checked);
        item->setText(1, tr("scanning..."));
        item->setData(0, RoleSize, 0);
        paths << QDir(QDir::homePath()).filePath(entry);
    }
    m_rescanButton->setEnabled(true);
    m_backupButton->setEnabled(!entries.isEmpty());

    if (m_scanner) {
        m_scanner->wait();
        m_scanner->deleteLater();
        m_scanner = nullptr;
    }
    if (!paths.isEmpty()) {
        m_scanner = new DirSizeScanner(paths, this);
        connect(m_scanner, &DirSizeScanner::sizeComputed, this, &UserBackupRestorePage::onEntrySizeComputed);
        connect(m_scanner, &DirSizeScanner::finished, this, &UserBackupRestorePage::onEntrySizeScanFinished);
        m_scanner->start();
    } else {
        updateSelectedSummary();
    }
}

void UserBackupRestorePage::onEntrySizeComputed(const QString &path, qint64 bytes)
{
    const QString home = QDir::homePath();
    for (int i = 0; i < m_entriesTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_entriesTree->topLevelItem(i);
        if (QDir(home).filePath(item->text(0)) == path) {
            item->setText(1, formatSize(bytes));
            item->setData(0, RoleSize, bytes);
            break;
        }
    }
    updateSelectedSummary();
}

void UserBackupRestorePage::onEntrySizeScanFinished()
{
    updateSelectedSummary();
}

void UserBackupRestorePage::onSelectAll(bool checked)
{
    for (int i = 0; i < m_entriesTree->topLevelItemCount(); ++i)
        m_entriesTree->topLevelItem(i)->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
}

void UserBackupRestorePage::onEntryChecked()
{
    updateSelectedSummary();
}

void UserBackupRestorePage::updateSelectedSummary()
{
    int count = 0;
    qint64 total = 0;
    for (int i = 0; i < m_entriesTree->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *item = m_entriesTree->topLevelItem(i);
        if (item->checkState(0) == Qt::Checked) {
            ++count;
            total += item->data(0, RoleSize).toLongLong();
        }
    }
    m_selectedSummaryLabel->setText(tr("%n item(s) selected, %1", nullptr, count).arg(formatSize(total)));
}

void UserBackupRestorePage::onCreateBackup()
{
    const QString destDir = m_backupDestEdit->text().trimmed();
    if (destDir.isEmpty() || !QDir(destDir).exists()) {
        QMessageBox::warning(this, tr("Backup"), tr("Choose a valid destination folder first."));
        return;
    }

    QStringList selected;
    for (int i = 0; i < m_entriesTree->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *item = m_entriesTree->topLevelItem(i);
        if (item->checkState(0) == Qt::Checked)
            selected << item->text(0);
    }
    if (selected.isEmpty()) {
        QMessageBox::information(this, tr("Backup"), tr("No dotfiles/folders selected."));
        return;
    }

    const bool compressed = m_compressedRadio->isChecked();
    const QString home = QDir::homePath();
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    setBusy(true, tr("Creating backup..."));

    QFuture<OperationResult> future =
        QtConcurrent::run([destDir, selected, compressed, home, timestamp]() -> OperationResult {
            OperationResult op;

            if (compressed) {
                const QString archivePath =
                    QDir(destDir).filePath(QStringLiteral("athena-userbackup-%1.tar.gz").arg(timestamp));
                QStringList args = {"-czf", archivePath, "-C", home};
                args << selected;
                const Result result = ProcessRunner::run("tar", args, 600000);
                op.success = result.started && result.exitCode == 0;
                op.output = op.success ? archivePath : result.stdOut + result.stdErr;
                return op;
            }

            const QString folderPath =
                QDir(destDir).filePath(QStringLiteral("athena-userbackup-%1").arg(timestamp));
            QDir().mkpath(folderPath);
            op.success = true;
            for (const QString &entry : selected) {
                const Result result = ProcessRunner::run(
                    "cp", {"-a", QDir(home).filePath(entry), folderPath}, 600000);
                if (!(result.started && result.exitCode == 0)) {
                    op.success = false;
                    op.output += result.stdOut + result.stdErr + '\n';
                }
            }
            if (op.success)
                op.output = folderPath;
            return op;
        });
    m_backupWatcher.setFuture(future);
}

void UserBackupRestorePage::onBackupFinished()
{
    const OperationResult result = m_backupWatcher.result();
    setBusy(false, QString());
    if (result.success) {
        m_statusLabel->setText(tr("Backup saved to %1").arg(result.output));
        QMessageBox::information(this, tr("Backup Complete"), tr("Backup saved successfully."));
    } else {
        m_statusLabel->clear();
        QMessageBox::critical(this, tr("Backup Failed"),
                               tr("The backup process failed.\n\n%1").arg(result.output.trimmed()));
    }
}

void UserBackupRestorePage::onBrowseRestoreArchive()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Choose Backup Archive"), QDir::homePath(),
                                                        tr("Athena backups (*.tar.gz);;All files (*)"));
    if (!file.isEmpty()) {
        m_restoreSourceEdit->setText(file);
        schedulePreviewScan();
    }
}

void UserBackupRestorePage::onBrowseRestoreFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose Backup Folder"), QDir::homePath());
    if (!dir.isEmpty()) {
        m_restoreSourceEdit->setText(dir);
        schedulePreviewScan();
    }
}

void UserBackupRestorePage::onRestoreSourceEdited()
{
    schedulePreviewScan();
}

void UserBackupRestorePage::schedulePreviewScan()
{
    const QString source = m_restoreSourceEdit->text().trimmed();
    if (source.isEmpty() || !QFileInfo::exists(source)) {
        m_previewList->clear();
        m_previewSummaryLabel->setText(tr("Choose a backup above to see what it contains."));
        return;
    }

    m_previewSummaryLabel->setText(tr("Reading contents..."));
    const bool isFolder = QFileInfo(source).isDir();
    QFuture<QStringList> future = QtConcurrent::run([source, isFolder]() { return topLevelEntries(source, isFolder); });
    m_previewWatcher.setFuture(future);
}

void UserBackupRestorePage::onPreviewScanned()
{
    const QStringList entries = m_previewWatcher.result();
    m_previewList->clear();
    for (const QString &entry : entries)
        new QListWidgetItem(entry, m_previewList);
    m_previewSummaryLabel->setText(entries.isEmpty() ? tr("No contents found — is this a valid backup?")
                                                      : tr("%n item(s) in this backup:", nullptr, entries.size()));
}

void UserBackupRestorePage::onRestore()
{
    const QString source = m_restoreSourceEdit->text().trimmed();
    if (source.isEmpty() || !QFileInfo::exists(source)) {
        QMessageBox::warning(this, tr("Restore"), tr("Choose a valid backup archive or folder first."));
        return;
    }

    const bool isFolder = QFileInfo(source).isDir();
    const QString home = QDir::homePath();

    PackageActions::confirmAndRun(
        this, tr("Restore User Data"),
        tr("This copies dotfiles/configuration from the backup into your home directory, overwriting "
           "any existing files with the same name. Continue?"),
        [source, isFolder, home]() -> OperationResult {
            OperationResult op;
            op.success = true;

            if (isFolder) {
                const QDir sourceDir(source);
                const QStringList entries =
                    sourceDir.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
                for (const QString &entry : entries) {
                    const Result result =
                        ProcessRunner::run("cp", {"-a", sourceDir.filePath(entry), home}, 600000);
                    if (!(result.started && result.exitCode == 0)) {
                        op.success = false;
                        op.output += result.stdOut + result.stdErr + '\n';
                    }
                }
            } else {
                const Result result = ProcessRunner::run("tar", {"-xzf", source, "-C", home}, 600000);
                op.success = result.started && result.exitCode == 0;
                op.output = result.stdOut + result.stdErr;
            }
            return op;
        },
        [this](bool success) {
            if (success)
                m_statusLabel->setText(tr("Restore complete."));
        },
        /*requiresPrivileges=*/false);
}

void UserBackupRestorePage::setBusy(bool busy, const QString &status)
{
    if (!status.isEmpty())
        m_statusLabel->setText(status);
    m_backupButton->setEnabled(!busy && m_entriesTree->topLevelItemCount() > 0);
    m_rescanButton->setEnabled(!busy);
    m_restoreButton->setEnabled(!busy);
}
