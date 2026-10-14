#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include "../core/PackageBackend.h"
#include "../core/SystemTools.h"

class QButtonGroup;
class QPlainTextEdit;
class QLabel;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

// A source's recent install/remove/update activity. For sources whose log
// SystemTools can parse (`activityKey`: "pacman", "apt", "dnf", "flatpak")
// it's a timeline, with the backend's raw log (PackageBackend::
// recentHistory()) a click away; otherwise just the raw log.
class HistoryPage : public QWidget {
    Q_OBJECT

public:
    explicit HistoryPage(PackageBackend *backend, QWidget *parent = nullptr, const QString &activityKey = QString());

public slots:
    void refresh();

private:
    struct Loaded {
        QString raw;
        QVector<SystemTools::Activity> activity;
    };
    void onLoaded();

    PackageBackend *m_backend;
    QString m_activityKey;
    QPlainTextEdit *m_view;
    QStackedWidget *m_views = nullptr;
    QVBoxLayout *m_timeline = nullptr;
    QLabel *m_statusLabel;
    QPushButton *m_refreshButton;
    QFutureWatcher<Loaded> m_watcher;
};
