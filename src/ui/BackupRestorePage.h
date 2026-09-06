#pragma once

#include <QFutureWatcher>
#include <QVector>
#include <QWidget>

#include "../core/backends/FlatpakBackend.h"

class QLineEdit;
class QPushButton;
class QCheckBox;
class QTableWidget;
class QLabel;
class QProgressBar;

// Backs up all Flatpak user data (~/.var/app) plus a manifest of
// installed apps to a single .tar.gz, and restores data or reinstalls
// apps from one — handy when moving to a new machine or a fresh OS
// install. Flatpak-only concept, so this takes a FlatpakBackend* directly
// rather than the generic PackageBackend interface.
class BackupRestorePage : public QWidget {
    Q_OBJECT

public:
    explicit BackupRestorePage(FlatpakBackend *backend, QWidget *parent = nullptr);

private slots:
    void onBrowseBackupDestination();
    void onCreateBackup();
    void onBackupFinished();
    void onBrowseRestoreSource();
    void onLoadManifest();
    void onManifestLoaded();
    void onInstallSelected();
    void onInstallFinished();
    void onSelectAllApps(bool checked);

private:
    struct ManifestApp {
        QString appId;
        QString name;
        QString origin;
        QString remoteUrl;
    };
    struct ManifestLoadResult {
        QVector<ManifestApp> apps;
        QString createdOn;
        QString error;
    };

    void populateManifestTable(const QVector<ManifestApp> &apps);
    void setBusy(bool busy, const QString &status);

    FlatpakBackend *m_backend;

    // Backup
    QLineEdit *m_backupDestEdit;
    QCheckBox *m_includeCacheCheck;
    QPushButton *m_backupButton;

    // Restore
    QLineEdit *m_restoreSourceEdit;
    QPushButton *m_loadManifestButton;
    QPushButton *m_restoreDataButton;
    QTableWidget *m_appsTable;
    QCheckBox *m_selectAllCheck;
    QPushButton *m_installSelectedButton;
    QProgressBar *m_progressBar;

    QLabel *m_statusLabel;

    QVector<ManifestApp> m_manifestApps;
    QString m_currentArchivePath;

    QFutureWatcher<OperationResult> m_backupWatcher;
    QFutureWatcher<ManifestLoadResult> m_manifestWatcher;
    QFutureWatcher<OperationResult> m_installWatcher;
};
