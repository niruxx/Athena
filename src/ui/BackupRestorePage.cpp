#include "BackupRestorePage.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "../core/ProcessRunner.h"
#include "AppIcons.h"
#include "PackageActions.h"

using ProcessRunner::Result;

namespace {
constexpr const char *kManifestFileName = "athena-manifest.json";
}

BackupRestorePage::BackupRestorePage(FlatpakBackend *backend, QWidget *parent) : QWidget(parent), m_backend(backend)
{
    auto *outer = new QVBoxLayout(this);

    auto *backupGroup = new QGroupBox(tr("Backup"), this);
    auto *backupLayout = new QVBoxLayout(backupGroup);
    backupLayout->addWidget(new QLabel(tr("Save all Flatpak application user data (~/.var/app) and the "
                                           "list of installed applications to an archive at a "
                                           "destination you choose."),
                                        backupGroup));

    auto *destRow = new QHBoxLayout;
    m_backupDestEdit = new QLineEdit(backupGroup);
    m_backupDestEdit->setPlaceholderText(tr("Destination folder..."));
    auto *browseDestButton = new QPushButton(tr("Browse..."), backupGroup);
    destRow->addWidget(m_backupDestEdit, 1);
    destRow->addWidget(browseDestButton);
    backupLayout->addLayout(destRow);

    m_includeCacheCheck = new QCheckBox(tr("Include cache data (larger, usually unnecessary)"), backupGroup);
    backupLayout->addWidget(m_includeCacheCheck);

    m_backupButton = new QPushButton(AppIcons::download(), tr("Back Up Now"), backupGroup);
    backupLayout->addWidget(m_backupButton, 0, Qt::AlignLeft);

    outer->addWidget(backupGroup);

    auto *restoreGroup = new QGroupBox(tr("Restore"), this);
    auto *restoreLayout = new QVBoxLayout(restoreGroup);
    restoreLayout->addWidget(new QLabel(tr("Restore user data from a backup, or reinstall the "
                                            "applications it lists — handy when moving to a new machine "
                                            "or a fresh OS install."),
                                         restoreGroup));

    auto *sourceRow = new QHBoxLayout;
    m_restoreSourceEdit = new QLineEdit(restoreGroup);
    m_restoreSourceEdit->setPlaceholderText(tr("Backup file (.tar.gz)..."));
    auto *browseSourceButton = new QPushButton(tr("Browse..."), restoreGroup);
    m_loadManifestButton = new QPushButton(tr("Load Backup Contents"), restoreGroup);
    sourceRow->addWidget(m_restoreSourceEdit, 1);
    sourceRow->addWidget(browseSourceButton);
    sourceRow->addWidget(m_loadManifestButton);
    restoreLayout->addLayout(sourceRow);

    m_appsTable = new QTableWidget(0, 3, restoreGroup);
    m_appsTable->setHorizontalHeaderLabels({tr("Application"), tr("ID"), tr("Remote")});
    m_appsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_appsTable->verticalHeader()->setVisible(false);
    m_appsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_appsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    restoreLayout->addWidget(m_appsTable, 1);

    auto *restoreButtonRow = new QHBoxLayout;
    m_selectAllCheck = new QCheckBox(tr("Select All"), restoreGroup);
    m_selectAllCheck->setChecked(true);
    m_restoreDataButton = new QPushButton(AppIcons::download(), tr("Restore User Data..."), restoreGroup);
    m_installSelectedButton = new QPushButton(AppIcons::install(), tr("Install Selected Applications"), restoreGroup);
    m_restoreDataButton->setEnabled(false);
    m_installSelectedButton->setEnabled(false);
    restoreButtonRow->addWidget(m_selectAllCheck);
    restoreButtonRow->addStretch(1);
    restoreButtonRow->addWidget(m_restoreDataButton);
    restoreButtonRow->addWidget(m_installSelectedButton);
    restoreLayout->addLayout(restoreButtonRow);

    m_progressBar = new QProgressBar(restoreGroup);
    m_progressBar->setRange(0, 0); // indeterminate — install is one background sweep, not per-app steps
    m_progressBar->setVisible(false);
    restoreLayout->addWidget(m_progressBar);

    outer->addWidget(restoreGroup, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    outer->addWidget(m_statusLabel);

    connect(browseDestButton, &QPushButton::clicked, this, &BackupRestorePage::onBrowseBackupDestination);
    connect(m_backupButton, &QPushButton::clicked, this, &BackupRestorePage::onCreateBackup);
    connect(browseSourceButton, &QPushButton::clicked, this, &BackupRestorePage::onBrowseRestoreSource);
    connect(m_loadManifestButton, &QPushButton::clicked, this, &BackupRestorePage::onLoadManifest);
    connect(m_installSelectedButton, &QPushButton::clicked, this, &BackupRestorePage::onInstallSelected);
    connect(m_selectAllCheck, &QCheckBox::toggled, this, &BackupRestorePage::onSelectAllApps);
    connect(&m_backupWatcher, &QFutureWatcher<OperationResult>::finished, this, &BackupRestorePage::onBackupFinished);
    connect(&m_manifestWatcher, &QFutureWatcher<ManifestLoadResult>::finished, this,
            &BackupRestorePage::onManifestLoaded);
    connect(&m_installWatcher, &QFutureWatcher<OperationResult>::finished, this,
            &BackupRestorePage::onInstallFinished);

    connect(m_restoreDataButton, &QPushButton::clicked, this, [this]() {
        if (m_currentArchivePath.isEmpty())
            return;
        const QString archivePath = m_currentArchivePath;
        PackageActions::confirmAndRun(
            this, tr("Restore User Data"),
            tr("This extracts application data from the backup into ~/.var/app, overwriting any "
               "existing data for the same application IDs. Continue?"),
            [archivePath]() -> OperationResult {
                const Result result = ProcessRunner::run(
                    "tar", {"-xzf", archivePath, "-C", QDir::homePath(), QStringLiteral(".var/app")}, 600000);
                OperationResult op;
                op.success = result.started && result.exitCode == 0;
                op.output = result.stdOut + result.stdErr;
                return op;
            },
            [this](bool success) {
                if (success)
                    m_statusLabel->setText(tr("Application data restored."));
            },
            /*requiresPrivileges=*/false);
    });

    setBusy(false, QString());
}

void BackupRestorePage::onBrowseBackupDestination()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose Backup Destination"), QDir::homePath());
    if (!dir.isEmpty())
        m_backupDestEdit->setText(dir);
}

void BackupRestorePage::onCreateBackup()
{
    const QString destDir = m_backupDestEdit->text().trimmed();
    if (destDir.isEmpty() || !QDir(destDir).exists()) {
        QMessageBox::warning(this, tr("Backup"), tr("Choose a valid destination folder first."));
        return;
    }

    const bool includeCache = m_includeCacheCheck->isChecked();
    FlatpakBackend *backend = m_backend;
    setBusy(true, tr("Creating backup..."));

    QFuture<OperationResult> future = QtConcurrent::run([backend, destDir, includeCache]() -> OperationResult {
        OperationResult op;

        const QVector<PackageInfo> apps = backend->listInstalled();
        const QVector<RepositoryInfo> remotes = backend->listRepositories();
        QHash<QString, QString> remoteUrls;
        for (const RepositoryInfo &remote : remotes)
            remoteUrls.insert(remote.id, remote.url);

        QJsonArray appsArray;
        for (const PackageInfo &app : apps) {
            QJsonObject obj;
            obj[QStringLiteral("id")] = app.name;
            obj[QStringLiteral("name")] = app.description.section(QStringLiteral(" — "), 0, 0);
            obj[QStringLiteral("origin")] = app.repository;
            obj[QStringLiteral("remoteUrl")] = remoteUrls.value(app.repository);
            appsArray.append(obj);
        }

        QJsonObject root;
        root[QStringLiteral("version")] = 1;
        root[QStringLiteral("created")] = QDateTime::currentDateTime().toString(Qt::ISODate);
        root[QStringLiteral("apps")] = appsArray;

        const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
            + QStringLiteral("/athena-backup-%1").arg(QDateTime::currentMSecsSinceEpoch());
        QDir().mkpath(tempDir);
        QFile manifestFile(tempDir + QLatin1Char('/') + QLatin1String(kManifestFileName));
        if (!manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            op.output = QStringLiteral("Could not write manifest file.");
            QDir(tempDir).removeRecursively();
            return op;
        }
        manifestFile.write(QJsonDocument(root).toJson());
        manifestFile.close();

        const QString archiveName = QStringLiteral("athena-backup-%1.tar.gz")
                                         .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
        const QString archivePath = QDir(destDir).filePath(archiveName);

        QStringList args = {"-czf", archivePath};
        if (!includeCache)
            args << QStringLiteral("--exclude=cache");
        args << "-C" << tempDir << QLatin1String(kManifestFileName);
        if (QDir(FlatpakBackend::userDataRoot()).exists())
            args << "-C" << QDir::homePath() << QStringLiteral(".var/app");

        const Result result = ProcessRunner::run("tar", args, 600000);
        QDir(tempDir).removeRecursively();

        op.success = result.started && result.exitCode == 0;
        op.output = op.success ? archivePath : result.stdOut + result.stdErr;
        return op;
    });
    m_backupWatcher.setFuture(future);
}

void BackupRestorePage::onBackupFinished()
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

void BackupRestorePage::onBrowseRestoreSource()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Choose Backup File"), QDir::homePath(),
                                                        tr("Athena backups (*.tar.gz);;All files (*)"));
    if (!file.isEmpty())
        m_restoreSourceEdit->setText(file);
}

void BackupRestorePage::onLoadManifest()
{
    const QString archivePath = m_restoreSourceEdit->text().trimmed();
    if (archivePath.isEmpty() || !QFile::exists(archivePath)) {
        QMessageBox::warning(this, tr("Restore"), tr("Choose a valid backup file first."));
        return;
    }
    m_currentArchivePath = archivePath;
    setBusy(true, tr("Reading backup contents..."));

    QFuture<ManifestLoadResult> future = QtConcurrent::run([archivePath]() -> ManifestLoadResult {
        ManifestLoadResult loadResult;

        const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
            + QStringLiteral("/athena-restore-%1").arg(QDateTime::currentMSecsSinceEpoch());
        QDir().mkpath(tempDir);
        // Extracting a single named member can make GNU tar report a
        // non-zero exit (it keeps scanning the rest of the archive after
        // finding it) even though the file was written correctly, so
        // presence of the file is the real signal here, not the exit code.
        ProcessRunner::run("tar", {"-xzf", archivePath, "-C", tempDir, QLatin1String(kManifestFileName)}, 60000);

        const QString manifestPath = tempDir + QLatin1Char('/') + QLatin1String(kManifestFileName);
        QFile file(manifestPath);
        if (!file.open(QIODevice::ReadOnly)) {
            loadResult.error = QStringLiteral("Could not read a manifest from this file. It may not be "
                                               "an Athena backup.");
            QDir(tempDir).removeRecursively();
            return loadResult;
        }

        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        loadResult.createdOn = root[QStringLiteral("created")].toString();
        for (const QJsonValue &value : root[QStringLiteral("apps")].toArray()) {
            const QJsonObject obj = value.toObject();
            ManifestApp app;
            app.appId = obj[QStringLiteral("id")].toString();
            app.name = obj[QStringLiteral("name")].toString();
            app.origin = obj[QStringLiteral("origin")].toString();
            app.remoteUrl = obj[QStringLiteral("remoteUrl")].toString();
            loadResult.apps << app;
        }
        QDir(tempDir).removeRecursively();
        return loadResult;
    });
    m_manifestWatcher.setFuture(future);
}

void BackupRestorePage::onManifestLoaded()
{
    const ManifestLoadResult result = m_manifestWatcher.result();
    setBusy(false, QString());

    if (!result.error.isEmpty()) {
        populateManifestTable({});
        QMessageBox::warning(this, tr("Restore"), result.error);
        return;
    }

    populateManifestTable(result.apps);
    m_statusLabel->setText(
        tr("Loaded %1 application(s) from a backup created %2.").arg(result.apps.size()).arg(result.createdOn));
}

void BackupRestorePage::populateManifestTable(const QVector<ManifestApp> &apps)
{
    m_manifestApps = apps;
    m_appsTable->setRowCount(apps.size());
    for (int i = 0; i < apps.size(); ++i) {
        const ManifestApp &app = apps.at(i);
        auto *nameItem = new QTableWidgetItem(app.name.isEmpty() ? app.appId : app.name);
        nameItem->setFlags(nameItem->flags() | Qt::ItemIsUserCheckable);
        nameItem->setCheckState(m_selectAllCheck->isChecked() ? Qt::Checked : Qt::Unchecked);
        m_appsTable->setItem(i, 0, nameItem);
        m_appsTable->setItem(i, 1, new QTableWidgetItem(app.appId));
        m_appsTable->setItem(i, 2, new QTableWidgetItem(app.origin));
    }
    m_restoreDataButton->setEnabled(!apps.isEmpty());
    m_installSelectedButton->setEnabled(!apps.isEmpty());
}

void BackupRestorePage::onSelectAllApps(bool checked)
{
    for (int row = 0; row < m_appsTable->rowCount(); ++row) {
        if (auto *item = m_appsTable->item(row, 0))
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
}

void BackupRestorePage::onInstallSelected()
{
    QVector<ManifestApp> selected;
    for (int row = 0; row < m_appsTable->rowCount(); ++row) {
        auto *item = m_appsTable->item(row, 0);
        if (item && item->checkState() == Qt::Checked)
            selected << m_manifestApps.at(row);
    }
    if (selected.isEmpty()) {
        QMessageBox::information(this, tr("Install"), tr("No applications selected."));
        return;
    }

    FlatpakBackend *backend = m_backend;
    setBusy(true, tr("Installing %1 application(s)...").arg(selected.size()));

    QFuture<OperationResult> future = QtConcurrent::run([backend, selected]() -> OperationResult {
        int successCount = 0;
        QStringList failures;
        for (const ManifestApp &app : selected) {
            if (!app.origin.isEmpty() && !app.remoteUrl.isEmpty()) {
                RepositoryAddValues values;
                values[QStringLiteral("name")] = app.origin;
                values[QStringLiteral("url")] = app.remoteUrl;
                backend->addRepository(values); // idempotent: --if-not-exists
            }
            const OperationResult result = backend->installPackages({app.appId});
            if (result.success)
                ++successCount;
            else
                failures << app.appId;
        }

        OperationResult op;
        op.success = failures.isEmpty();
        op.output = op.success ? QString()
                                : QStringLiteral("Installed %1 of %2. Failed: %3")
                                      .arg(successCount)
                                      .arg(selected.size())
                                      .arg(failures.join(QStringLiteral(", ")));
        return op;
    });
    m_installWatcher.setFuture(future);
}

void BackupRestorePage::onInstallFinished()
{
    const OperationResult result = m_installWatcher.result();
    setBusy(false, QString());
    if (result.success) {
        m_statusLabel->setText(tr("Selected applications installed."));
    } else {
        m_statusLabel->clear();
        QMessageBox::warning(this, tr("Install"), result.output);
    }
}

void BackupRestorePage::setBusy(bool busy, const QString &status)
{
    if (!status.isEmpty())
        m_statusLabel->setText(status);
    m_backupButton->setEnabled(!busy);
    m_loadManifestButton->setEnabled(!busy);
    m_restoreDataButton->setEnabled(!busy && !m_manifestApps.isEmpty());
    m_installSelectedButton->setEnabled(!busy && !m_manifestApps.isEmpty());
    m_progressBar->setVisible(busy);
}
