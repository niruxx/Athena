#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include "../core/SystemTools.h"

class QLabel;
class QListWidget;
class QPushButton;

// Tools → Snapshots: the system snapshots Snapper or Timeshift has taken
// (Athena's own before-transaction ones included), taking one now,
// deleting one, and restoring one.
class SnapshotsPage : public QWidget {
    Q_OBJECT

public:
    explicit SnapshotsPage(QWidget *parent = nullptr);

public slots:
    // Lists without a password prompt where the tool allows it.
    void refresh();

private:
    struct Loaded {
        QVector<SystemTools::Snapshot> snapshots;
        QString error;
        bool asAdmin = false;
    };

    void load(bool asAdmin);
    void onLoaded();
    void takeSnapshot();
    void deleteSelected();
    void restoreSelected();
    void updateActions();
    const SystemTools::Snapshot *selected() const;

    SystemTools::SnapshotTool m_tool;
    QVector<SystemTools::Snapshot> m_snapshots;
    bool m_loadedAsAdmin = false;
    QLabel *m_status;
    QListWidget *m_list;
    QPushButton *m_takeButton;
    QPushButton *m_refreshButton;
    QPushButton *m_adminButton;
    QPushButton *m_deleteButton;
    QPushButton *m_restoreButton;
    QFutureWatcher<Loaded> m_watcher;
};
