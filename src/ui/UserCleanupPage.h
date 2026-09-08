#pragma once

#include <QFutureWatcher>
#include <QIcon>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QTreeWidget;
class QPushButton;
class QLabel;
class QFrame;

// One cleanable item shown as a row in a UserCleanupPage: a label plus the
// path(s) it covers (more than one for things like shell history, which is
// scattered across several dotfiles rather than a single directory).
struct CleanupTarget {
    QString label;
    QIcon icon;
    QStringList paths;
    QString description;
};

// Predefined target lists for the User Management group's General and
// Advanced tabs.
namespace UserCleanupTargets {
QVector<CleanupTarget> general();
QVector<CleanupTarget> advanced();
} // namespace UserCleanupTargets

// Generic "list of known cleanable locations, with sizes, delete what you
// select" page — used for both the General tab (temp files, cache) and the
// Advanced tab (trash, thumbnail cache, shell history, ...) with a
// different fixed target list and intro blurb each time. Every delete only
// removes entries owned by the current user (see UserCleanupUtils), so
// it's always unprivileged, plain file I/O.
class UserCleanupPage : public QWidget {
    Q_OBJECT

public:
    UserCleanupPage(const QString &introText, const QVector<CleanupTarget> &targets, QWidget *parent = nullptr);

public slots:
    void refresh();

private slots:
    void onSizesComputed();
    void onSelectionChanged();
    void onDeleteSelected();
    void onDeleteAll();

private:
    void deleteTargets(const QVector<int> &rows);
    void updateDetailsPanel();

    QVector<CleanupTarget> m_targets;

    QTreeWidget *m_tree;
    QFrame *m_detailsFrame;
    QLabel *m_detailsIcon;
    QLabel *m_detailsTitle;
    QLabel *m_detailsDescription;
    QLabel *m_detailsLocation;
    QPushButton *m_refreshButton;
    QPushButton *m_deleteButton;
    QPushButton *m_deleteAllButton;
    QLabel *m_totalLabel;

    QFutureWatcher<QVector<qint64>> m_sizeWatcher;
};
