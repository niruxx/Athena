#pragma once

#include <QFutureWatcher>
#include <QVector>
#include <QWidget>

#include "../core/PackageBackend.h"

class QTableWidget;
class QTableWidgetItem;
class QLabel;
class QPushButton;

// One backend to pull repositories from, tagged with a short label shown
// in the Source column (e.g. "System", "Flatpak").
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
    void onItemChanged(QTableWidgetItem *item);
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
    QTableWidget *m_table;
    QLabel *m_statusLabel;
    QPushButton *m_refreshButton;
    QPushButton *m_addButton;
    QPushButton *m_updateListsButton;
    bool m_updatingTable = false;

    QFutureWatcher<QVector<RepositoryRow>> m_listWatcher;
    QFutureWatcher<OperationResult> m_toggleWatcher;
    QFutureWatcher<OperationResult> m_addWatcher;
};
