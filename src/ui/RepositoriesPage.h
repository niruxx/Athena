#pragma once

#include <QFutureWatcher>
#include <QVector>
#include <QWidget>

#include "../core/PackageBackend.h"

class M3Switch;
class QLabel;
class QPushButton;
class QVBoxLayout;

// One backend to pull repositories from, tagged with a short label shown
// beside each repository (e.g. "System", "Flatpak").
struct RepositorySource {
    PackageBackend *backend = nullptr;
    QString label;
};

struct RepositoryRow {
    PackageBackend *backend = nullptr;
    QString sourceLabel;
    RepositoryInfo info;
};

class RepositoriesPage : public QWidget {
    Q_OBJECT

public:
    explicit RepositoriesPage(const QVector<RepositorySource> &sources, QWidget *parent = nullptr);

public slots:
    void refresh();

private slots:
    void onLoaded();
    void onSwitchToggled(int row, bool enabled);
    void onToggleFinished();
    void onAddRepositoryClicked();
    void onAddFinished();
    void onUpdateListsClicked();
    void onUpdateListsFinished();

private:
    void setBusy(bool busy);
    void applyToggle(int row, bool enabled);
    PackageBackend *pickSourceBackend();

    QVector<RepositorySource> m_sources;
    QVector<RepositoryRow> m_rows;
    // One row per repository: name, URL and source, and an on/off switch.
    QVBoxLayout *m_rowsLayout;
    QVector<M3Switch *> m_switches;
    QLabel *m_statusLabel;
    QPushButton *m_refreshButton;
    QPushButton *m_addButton;
    QPushButton *m_updateListsButton;

    QFutureWatcher<QVector<RepositoryRow>> m_listWatcher;
    QFutureWatcher<OperationResult> m_toggleWatcher;
    QFutureWatcher<OperationResult> m_addWatcher;
};
