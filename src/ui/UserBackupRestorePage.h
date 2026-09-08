#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include "../core/PackageBackend.h"

class QLineEdit;
class QPushButton;
class QRadioButton;
class QCheckBox;
class QListWidget;
class QLabel;

// Backs up the user's dotfiles/configuration (hidden top-level entries
// under $HOME) to either a single .tar.gz archive or a plain uncompressed
// copy in a destination folder the user chooses, and restores either kind
// back into $HOME — handy when moving to a new machine or a fresh OS
// install. Pure file I/O (tar/cp), no package backend involved.
class UserBackupRestorePage : public QWidget {
    Q_OBJECT

public:
    explicit UserBackupRestorePage(QWidget *parent = nullptr);

private slots:
    void onBrowseBackupDestination();
    void onRescanEntries();
    void onEntriesScanned();
    void onSelectAll(bool checked);
    void onCreateBackup();
    void onBackupFinished();
    void onBrowseRestoreArchive();
    void onBrowseRestoreFolder();
    void onRestore();

private:
    void setBusy(bool busy, const QString &status);

    // Backup
    QLineEdit *m_backupDestEdit;
    QRadioButton *m_compressedRadio;
    QRadioButton *m_copyRadio;
    QListWidget *m_entriesList;
    QCheckBox *m_selectAllCheck;
    QPushButton *m_rescanButton;
    QPushButton *m_backupButton;

    // Restore
    QLineEdit *m_restoreSourceEdit;
    QPushButton *m_restoreButton;

    QLabel *m_statusLabel;

    QFutureWatcher<QStringList> m_scanWatcher;
    QFutureWatcher<OperationResult> m_backupWatcher;
};
