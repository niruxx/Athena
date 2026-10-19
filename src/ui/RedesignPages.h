#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include "../core/SystemTools.h"
#include "Catalog.h"

class CatalogHub;
class DetailsSheet;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QStackedWidget;
class QTimer;
class QVBoxLayout;
class StackedBar;

// The navigation-rail pages of the Material You redesign. Each works
// across every source at once, through CatalogHub; actions on a single
// item go through the shared DetailsSheet.

// Home: the system at a glance — pending updates by source, Arch news to
// read before updating, cleanup, installed totals, AUR reviews waiting,
// snapshots, and a cross-source activity timeline.
class HomePage : public QWidget {
    Q_OBJECT

public:
    HomePage(CatalogHub *hub, PackageBackend *native, const QString &nativeKey, QWidget *parent = nullptr);

    // Arch news posts published since the last full system upgrade that
    // haven't been read or dismissed (checked before "Update everything").
    QVector<SystemTools::NewsItem> unreadNews() const;
    void markNewsRead();

signals:
    void navigate(const QString &page);
    void updateEverything();
    void reviewAurUpdates();

private:
    void refreshUpdates();
    void refreshInstalled();
    void loadSideStats();
    void showNews();
    void showActivity(const QVector<SystemTools::Activity> &activity);

    CatalogHub *m_hub;
    PackageBackend *m_native;
    QString m_nativeKey;
    class QFrame *m_newsCard;
    QLabel *m_newsKicker;
    QLabel *m_newsTitle;
    QLabel *m_newsText;
    QVector<SystemTools::NewsItem> m_news;
    QDateTime m_lastUpgrade;
    QFutureWatcher<QPair<QVector<SystemTools::NewsItem>, QDateTime>> m_newsWatcher;
    QLabel *m_snapshotMetric = nullptr;
    QLabel *m_snapshotText = nullptr;
    QVBoxLayout *m_activityRows;
    QLabel *m_updateCount;
    QLabel *m_updateSummary;
    StackedBar *m_updateBar;
    QLabel *m_updateLegend;
    QPushButton *m_updateAllButton;
    QPushButton *m_seeUpdatesButton;
    QLabel *m_cacheMetric;
    QLabel *m_installedMetric;
    QLabel *m_installedText;
    QLabel *m_aurMetric;
    QLabel *m_aurText;
    QPushButton *m_aurButton;
    struct SideStats {
        qint64 cacheBytes = -1;
        QVector<SystemTools::Activity> activity;
        QVector<SystemTools::Snapshot> snapshots;
    };
    QFutureWatcher<SideStats> m_statsWatcher;
};

// Discover: one search across every source — the AppStream index of every
// app the sources offer (installed or not) plus each backend's own search —
// with results as app cards (merged across sources) or a dense package
// list. With no search, category chips browse the index.
class DiscoverPage : public QWidget {
    Q_OBJECT

public:
    DiscoverPage(CatalogHub *hub, DetailsSheet *sheet, QWidget *parent = nullptr);
    void focusSearch();

private:
    void runSearch();
    void showResults();
    // Fills the Repository/Architecture filters from the current results.
    void populateFilters();
    void updateSelectionBar();
    CatalogModel *currentModel() const;

    CatalogHub *m_hub;
    DetailsSheet *m_sheet;
    QLineEdit *m_search;
    QButtonGroup *m_sourceChips;
    QButtonGroup *m_mode;
    QWidget *m_categoryRow;
    QWidget *m_categoryScroll;
    QButtonGroup *m_categoryChips;
    // Package groups (pacman groups, comps groups, tasksel tasks): a chip
    // after the categories, then a picker for the group.
    // Advanced: Dependency Query and repository/architecture filters.
    QPushButton *m_advancedToggle;
    QWidget *m_advancedRow;
    QCheckBox *m_dependencyCheck;
    QComboBox *m_dependencyDirection;
    QComboBox *m_repositoryFilter;
    QComboBox *m_architectureFilter;
    // Batch install/download.
    QPushButton *m_selectToggle;
    QFrame *m_selectionBar;
    QLabel *m_selectionCount;
    QPushButton *m_installSelected;
    QPushButton *m_downloadSelected;
    QLabel *m_heading;
    QStackedWidget *m_views;
    QListView *m_cards;
    QListView *m_rows;
    CatalogModel *m_cardModel;
    CatalogModel *m_rowModel;
    QTimer *m_debounce;
    QVector<CatalogItem> m_results; // unmerged
    QString m_activeQuery;
    int m_generation = 0;
    int m_pending = 0;
};

// Installed: everything installed, from every source, with an "Apps only"
// filter for desktop applications.
class LibraryPage : public QWidget {
    Q_OBJECT

public:
    LibraryPage(CatalogHub *hub, DetailsSheet *sheet, QWidget *parent = nullptr);

private:
    void apply();

    CatalogHub *m_hub;
    DetailsSheet *m_sheet;
    QButtonGroup *m_sourceChips;
    QPushButton *m_appsOnly;
    QPushButton *m_selectToggle;
    class QFrame *m_selectionBar;
    QLabel *m_selectionCount;
    QPushButton *m_removeSelected;
    QLineEdit *m_filter;
    QLabel *m_count;
    QPushButton *m_showAll;
    QListView *m_list;
    CatalogModel *m_model;
};

// Updates: every pending update, grouped by source, with per-source and
// update-everything actions.
class AllUpdatesPage : public QWidget {
    Q_OBJECT

public:
    AllUpdatesPage(CatalogHub *hub, DetailsSheet *sheet, QWidget *parent = nullptr);

signals:
    void updateEverything();

private:
    void rebuild();

    CatalogHub *m_hub;
    DetailsSheet *m_sheet;
    QLabel *m_summary;
    QPushButton *m_updateAll;
    QVBoxLayout *m_sections;
    QWidget *m_empty;
    QPushButton *m_selectToggle;
    class QFrame *m_selectionBar;
    QLabel *m_selectionCount;
    QPushButton *m_updateSelected;
    QVector<CatalogModel *> m_sectionModels;
    void updateSelectionBar();
};

// Sources: per-source tools from before the redesign (repositories,
// groups, history, Flatpak permissions, the classic package browser...),
// one tab set per source, picked with chips. These move into the new pages
// over time (see the README's TODO list).
class SourcesPage : public QWidget {
    Q_OBJECT

public:
    explicit SourcesPage(QWidget *parent = nullptr);
    void addSource(const QString &label, const QString &icon, QWidget *tabs);
    void showSource(int index);
    int indexOfSource(const QString &label) const;

private:
    class QHBoxLayout *m_chipRow;
    QButtonGroup *m_chips;
    QStackedWidget *m_stack;
};
