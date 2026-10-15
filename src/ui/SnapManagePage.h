#pragma once

#include <QFutureWatcher>
#include <QVector>
#include <QWidget>

#include "../core/SnapTools.h"

class M3Switch;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

// Sources → Snap → Manage: everything about an installed snap besides
// installing and removing it — which channel it follows, going back to the
// revision before its last refresh, turning it off without removing it,
// and its interface connections (what a confined snap may reach outside
// its sandbox; the Snap counterpart of Flatpak's permissions).
class SnapManagePage : public QWidget {
    Q_OBJECT

public:
    explicit SnapManagePage(QWidget *parent = nullptr);

public slots:
    void refresh();

private:
    struct Loaded {
        SnapTools::Details details;
        QVector<SnapTools::Connection> connections;
    };

    void onListLoaded();
    void loadCurrent();
    void onDetailsLoaded();
    void rebuildConnections(const QVector<SnapTools::Connection> &connections, bool classic);
    // Runs privileged commands in the terminal window, then reloads.
    void runInTerminal(const QString &title, const QVector<ProcessRunner::Command> &commands);
    QString currentName() const;
    const SnapTools::Snap *currentSnap() const;

    QVector<SnapTools::Snap> m_snaps;
    QListWidget *m_list;
    QLabel *m_listStatus;
    QPushButton *m_refreshButton;

    QStackedWidget *m_stack;
    QWidget *m_detail;
    QLabel *m_icon;
    QLabel *m_title;
    QLabel *m_summary;
    QLabel *m_meta;
    QLabel *m_tracking;
    QComboBox *m_channelCombo;
    QPushButton *m_switchChannelButton;
    M3Switch *m_enabledSwitch;
    QLabel *m_revertText;
    QPushButton *m_revertButton;
    QVBoxLayout *m_connectionRows;
    QLabel *m_connectionsNote;
    QString m_trackingChannel;
    bool m_updating = false;

    QFutureWatcher<QVector<SnapTools::Snap>> m_listWatcher;
    QFutureWatcher<Loaded> m_detailsWatcher;
    QFutureWatcher<ProcessRunner::Result> m_connectWatcher;
};
