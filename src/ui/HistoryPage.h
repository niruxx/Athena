#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include "../core/PackageBackend.h"

class QPlainTextEdit;
class QLabel;
class QPushButton;

// Read-only viewer for recent install/remove/update activity, via
// PackageBackend::recentHistory(). Shown as the backend's own raw output
// rather than parsed into a table — see recentHistory()'s doc comment for
// why (the formats don't share enough structure to unify cleanly).
class HistoryPage : public QWidget {
    Q_OBJECT

public:
    explicit HistoryPage(PackageBackend *backend, QWidget *parent = nullptr);

public slots:
    void refresh();

private:
    void onLoaded();

    PackageBackend *m_backend;
    QPlainTextEdit *m_view;
    QLabel *m_statusLabel;
    QPushButton *m_refreshButton;
    QFutureWatcher<QString> m_watcher;
};
