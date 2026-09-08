#include "UserBackupRestorePage.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSet>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "../core/ProcessRunner.h"
#include "AppIcons.h"
#include "PackageActions.h"

using ProcessRunner::Result;

namespace {
// Hidden top-level entries under $HOME that are transient/regenerable
// rather than actual configuration — unchecked by default so a routine
// backup doesn't balloon in size, though the user can still opt in.
const QSet<QString> kDefaultUncheckedEntries = {QStringLiteral(".cache")};
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
    auto *browseDestButton = new QPushButton(tr("Browse..."), backupGroup);
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

    m_entriesList = new QListWidget(backupGroup);
    m_entriesList->setSelectionMode(QAbstractItemView::NoSelection);
    backupLayout->addWidget(m_entriesList, 1);

    auto *entriesButtonRow = new QHBoxLayout;
    m_selectAllCheck = new QCheckBox(tr("Select All"), backupGroup);
    m_selectAllCheck->setChecked(true);
    m_rescanButton = new QPushButton(AppIcons::refresh(), tr("Rescan"), backupGroup);
    entriesButtonRow->addWidget(m_selectAllCheck);
    entriesButtonRow->addStretch(1);
    entriesButtonRow->addWidget(m_rescanButton);
    backupLayout->addLayout(entriesButtonRow);

    m_backupButton = new QPushButton(AppIcons::download(), tr("Back Up Now"), backupGroup);
    backupLayout->addWidget(m_backupButton, 0, Qt::AlignLeft);

    outer->addWidget(backupGroup, 1);

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
    auto *browseArchiveButton = new QPushButton(tr("Browse Archive..."), restoreGroup);
    auto *browseFolderButton = new QPushButton(tr("Browse Folder..."), restoreGroup);
    sourceRow->addWidget(m_restoreSourceEdit, 1);
    sourceRow->addWidget(browseArchiveButton);
    sourceRow->addWidget(browseFolderButton);
    restoreLayout->addLayout(sourceRow);

    m_restoreButton = new QPushButton(AppIcons::download(), tr("Restore Now..."), restoreGroup);
    restoreLayout->addWidget(m_restoreButton, 0, Qt::AlignLeft);

    outer->addWidget(restoreGroup);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    outer->addWidget(m_statusLabel);

    connect(browseDestButton, &QPushButton::clicked, this, &UserBackupRestorePage::onBrowseBackupDestination);
    connect(m_rescanButton, &QPushButton::clicked, this, &UserBackupRestorePage::onRescanEntries);
    connect(m_selectAllCheck, &QCheckBox::toggled, this, &UserBackupRestorePage::onSelectAll);
    connect(m_backupButton, &QPushButton::clicked, this, &UserBackupRestorePage::onCreateBackup);
    connect(browseArchiveButton, &QPushButton::clicked, this, &UserBackupRestorePage::onBrowseRestoreArchive);
    connect(browseFolderButton, &QPushButton::clicked, this, &UserBackupRestorePage::onBrowseRestoreFolder);
    connect(m_restoreButton, &QPushButton::clicked, this, &UserBackupRestorePage::onRestore);
    connect(&m_scanWatcher, &QFutureWatcher<QStringList>::finished, this, &UserBackupRestorePage::onEntriesScanned);
    connect(&m_backupWatcher, &QFutureWatcher<OperationResult>::finished, this,
            &UserBackupRestorePage::onBackupFinished);

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
    m_entriesList->clear();
    const QStringList entries = m_scanWatcher.result();
    for (const QString &entry : entries) {
        auto *item = new QListWidgetItem(entry, m_entriesList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(kDefaultUncheckedEntries.contains(entry) ? Qt::Unchecked : Qt::Checked);
    }
    m_rescanButton->setEnabled(true);
    m_backupButton->setEnabled(!entries.isEmpty());
}

void UserBackupRestorePage::onSelectAll(bool checked)
{
    for (int i = 0; i < m_entriesList->count(); ++i)
        m_entriesList->item(i)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
}

void UserBackupRestorePage::onCreateBackup()
{
    const QString destDir = m_backupDestEdit->text().trimmed();
    if (destDir.isEmpty() || !QDir(destDir).exists()) {
        QMessageBox::warning(this, tr("Backup"), tr("Choose a valid destination folder first."));
        return;
    }

    QStringList selected;
    for (int i = 0; i < m_entriesList->count(); ++i) {
        if (m_entriesList->item(i)->checkState() == Qt::Checked)
            selected << m_entriesList->item(i)->text();
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
    if (!file.isEmpty())
        m_restoreSourceEdit->setText(file);
}

void UserBackupRestorePage::onBrowseRestoreFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose Backup Folder"), QDir::homePath());
    if (!dir.isEmpty())
        m_restoreSourceEdit->setText(dir);
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
    m_backupButton->setEnabled(!busy && m_entriesList->count() > 0);
    m_rescanButton->setEnabled(!busy);
    m_restoreButton->setEnabled(!busy);
}
