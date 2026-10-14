#pragma once

#include <QFutureWatcher>

#include "../core/PackageBackend.h"
#include "../core/SystemTools.h"
#include "M3Dialog.h"

class QCheckBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;

// "Free up space": the package cache's size and the cleanup options for
// this package manager (old versions, uninstalled packages) plus unused
// dependencies, as checkboxes; the chosen ones run in one terminal window.
class CleanupDialog : public M3Dialog {
    Q_OBJECT

public:
    CleanupDialog(const QString &nativeKey, PackageBackend *backend, QWidget *parent = nullptr);
    // Runs the dialog and, if confirmed, the cleanup. True if anything ran
    // successfully.
    bool runModal();

private:
    struct Scan {
        qint64 cacheBytes = -1;
        QVector<ProcessRunner::Command> orphanCommands;
        int orphanCount = -1;
    };
    QVector<SystemTools::CacheCleanup> m_options;
    QVector<QCheckBox *> m_optionChecks;
    QCheckBox *m_orphansCheck;
    QLabel *m_sizeLabel;
    QPushButton *m_runButton;
    QFutureWatcher<Scan> m_watcher;
    Scan m_scan;
};

// One Arch news post, readable in place.
class NewsDialog : public M3Dialog {
    Q_OBJECT

public:
    NewsDialog(const SystemTools::NewsItem &item, QWidget *parent = nullptr);
};

// Installs a different version of a pacman package, from the local package
// cache or the Arch Linux Archive.
class DowngradeDialog : public M3Dialog {
    Q_OBJECT

public:
    DowngradeDialog(const QString &package, const QString &installedVersion, QWidget *parent = nullptr);
    // True if a version was installed.
    bool runModal();

private:
    QString m_package;
    QListWidget *m_list;
    QProgressBar *m_progress;
    QLabel *m_status;
    QPushButton *m_installButton;
    QVector<SystemTools::PackageVersion> m_versions;
    QFutureWatcher<QVector<SystemTools::PackageVersion>> m_watcher;
};
