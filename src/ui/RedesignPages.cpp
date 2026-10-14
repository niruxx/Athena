#include "RedesignPages.h"

#include <algorithm>
#include <memory>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QMenu>
#include <QSignalBlocker>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QMap>
#include <QPersistentModelIndex>
#include <QEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QPushButton>
#include <QTextDocument>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "../core/AppSettings.h"
#include "CatalogActions.h"
#include "CatalogHub.h"
#include "DetailsSheet.h"
#include "M3Widgets.h"
#include "MaterialTheme.h"
#include "Motion.h"
#include "PackageActions.h"
#include "ToolsDialogs.h"

namespace {

// A page that scrolls as a whole (Home, Updates), with the standard page
// margins; returns the layout to fill.
QVBoxLayout *scrollingPage(QWidget *page)
{
    auto *content = new QWidget(page);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 4, 24, 32);
    layout->setSpacing(24);

    auto *scroll = new QScrollArea(page);
    scroll->setProperty("m3", QStringLiteral("page"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);

    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
    return layout;
}

QListView *catalogList(CatalogModel *model, QAbstractItemDelegate *delegate, QWidget *parent)
{
    auto *view = new QListView(parent);
    view->setProperty("m3", QStringLiteral("plain"));
    view->setModel(model);
    view->setItemDelegate(delegate);
    view->setFrameShape(QFrame::NoFrame);
    view->setMouseTracking(true);
    view->setUniformItemSizes(true);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->viewport()->setAutoFillBackground(false);
    view->setCursor(Qt::PointingHandCursor);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    Motion::trackHover(view);
    Motion::crossfadeOnReset(view);
    return view;
}

// Stretches a card grid's cells so whole cards fill each row instead of
// leaving a ragged gap on the right.
class GridFitter : public QObject {
public:
    GridFitter(QListView *view, CatalogCardDelegate *delegate) : QObject(view), m_view(view), m_delegate(delegate)
    {
        view->viewport()->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Resize) {
            // A few pixels of slack: QListView wraps a row whose cells
            // exactly fill the viewport.
            const int available = m_view->viewport()->width() - 8;
            const int minimum = CatalogCardDelegate::kCardWidth + 12;
            const int columns = std::max(1, available / minimum);
            const QSize grid(available / columns, CatalogCardDelegate::kCardHeight + 12);
            if (m_view->gridSize() != grid) {
                m_delegate->setCardWidth(grid.width());
                m_view->setGridSize(grid);
            }
        }
        return false;
    }

private:
    QListView *m_view;
    CatalogCardDelegate *m_delegate;
};

// Exact name matches first, then names starting with the query, then the
// rest in the order the sources returned them.
int relevance(const CatalogItem &item, const QString &query)
{
    const QString q = query.toLower();
    int best = 3;
    QStringList names = {item.title.toLower()};
    for (const CatalogVariant &v : item.variants)
        names << v.info.name.toLower() << v.info.name.toLower().section('.', -1);
    for (const QString &name : names) {
        if (name == q)
            return 0;
        if (name.startsWith(q))
            best = std::min(best, 1);
        else if (name.contains(q))
            best = std::min(best, 2);
    }
    return best;
}

// Opens the sheet for whichever row/card was activated (click or Enter).
// Opens the sheet for whichever row/card was activated (click or Enter) —
// or, in selection mode, toggles the row. Styles that activate on single
// click emit both clicked() and activated() for one click; only the first
// counts.
void openOnActivate(QListView *view, CatalogModel *model, DetailsSheet *sheet, bool offerUpdate = false)
{
    auto handle = [model, sheet, offerUpdate](const QModelIndex &index) {
        static QElapsedTimer last;
        static QPersistentModelIndex lastIndex;
        if (!index.isValid() || (index == lastIndex && last.isValid() && last.elapsed() < 150))
            return;
        lastIndex = index;
        last.restart();
        if (model->selectionMode())
            model->toggleSelected(index.row());
        else
            sheet->open(model->item(index.row()), offerUpdate);
    };
    QObject::connect(view, &QListView::clicked, view, handle);
    QObject::connect(view, &QListView::activated, view, handle);
}

// The contextual bar shown while selecting: count, select all, the batch
// action, and done.
QFrame *selectionBar(QWidget *parent, QLabel *&count, QPushButton *&selectAll, QPushButton *&primary, QPushButton *&done,
                     const QString &primaryText, const QString &primaryRole, const QString &primaryIcon)
{
    QFrame *bar = M3::card(QStringLiteral("card"), parent);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(16, 8, 8, 8);
    count = M3::label(QString(), QStringLiteral("titleM"), bar);
    selectAll = new QPushButton(QObject::tr("Select all"), bar);
    MaterialTheme::styleButton(selectAll, QStringLiteral("text"), QStringLiteral("select_all"));
    primary = new QPushButton(primaryText, bar);
    MaterialTheme::styleButton(primary, primaryRole, primaryIcon);
    done = new QPushButton(QObject::tr("Done"), bar);
    MaterialTheme::styleButton(done, QStringLiteral("text"));
    layout->addWidget(count, 1);
    layout->addWidget(selectAll);
    layout->addWidget(primary);
    layout->addWidget(done);
    bar->hide();
    return bar;
}

bool itemFromSource(const CatalogItem &item, int source)
{
    return std::any_of(item.variants.begin(), item.variants.end(), [source](const CatalogVariant &v) { return v.source == source; });
}


// Freedesktop main categories, as AppStream catalogs use them.
struct Category {
    const char *key;
    const char *label;
    const char *icon;
};
const Category kCategories[] = {
    {"AudioVideo", QT_TRANSLATE_NOOP("DiscoverPage", "Audio & Video"), "music_note"},
    {"Development", QT_TRANSLATE_NOOP("DiscoverPage", "Development"), "code"},
    {"Education", QT_TRANSLATE_NOOP("DiscoverPage", "Education"), "school"},
    {"Game", QT_TRANSLATE_NOOP("DiscoverPage", "Games"), "sports_esports"},
    {"Graphics", QT_TRANSLATE_NOOP("DiscoverPage", "Graphics"), "brush"},
    {"Network", QT_TRANSLATE_NOOP("DiscoverPage", "Internet"), "public"},
    {"Office", QT_TRANSLATE_NOOP("DiscoverPage", "Office"), "description"},
    {"Science", QT_TRANSLATE_NOOP("DiscoverPage", "Science"), "science"},
    {"System", QT_TRANSLATE_NOOP("DiscoverPage", "System"), "settings_suggest"},
    {"Utility", QT_TRANSLATE_NOOP("DiscoverPage", "Utilities"), "handyman"},
};

QColor segmentColor(int index)
{
    const MaterialTheme::Scheme &s = MaterialTheme::current();
    QColor faded = s.onPrimaryContainer;
    switch (index % 4) {
    case 0:
        return s.primary;
    case 1:
        faded.setAlphaF(0.55f);
        return faded;
    case 2:
        faded.setAlphaF(0.30f);
        return faded;
    default:
        return s.outline;
    }
}

} // namespace

// ---------------------------------------------------------------- Home

HomePage::HomePage(CatalogHub *hub, PackageBackend *native, const QString &nativeKey, QWidget *parent)
    : QWidget(parent), m_hub(hub), m_native(native), m_nativeKey(nativeKey)
{
    QVBoxLayout *layout = scrollingPage(this);

    // Hero: pending updates.
    QFrame *hero = M3::card(QStringLiteral("hero"), this);
    auto *heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(28, 24, 28, 24);
    heroLayout->setSpacing(14);
    heroLayout->addWidget(M3::label(tr("Ready to update"), QStringLiteral("titleM"), hero));
    m_updateCount = M3::label(QStringLiteral("…"), QStringLiteral("display"), hero);
    heroLayout->addWidget(m_updateCount);
    m_updateSummary = new QLabel(tr("Checking every source for updates…"), hero);
    heroLayout->addWidget(m_updateSummary);
    m_updateBar = new StackedBar(hero);
    heroLayout->addWidget(m_updateBar);
    m_updateLegend = new QLabel(hero);
    m_updateLegend->setTextFormat(Qt::RichText);
    heroLayout->addWidget(m_updateLegend);
    auto *heroButtons = new QHBoxLayout;
    m_updateAllButton = new QPushButton(tr("Update everything"), hero);
    MaterialTheme::styleButton(m_updateAllButton, QStringLiteral("filled"), QStringLiteral("system_update_alt"));
    m_seeUpdatesButton = new QPushButton(tr("See what's changing"), hero);
    MaterialTheme::styleButton(m_seeUpdatesButton, QStringLiteral("outlined"));
    heroButtons->addWidget(m_updateAllButton);
    heroButtons->addWidget(m_seeUpdatesButton);
    heroButtons->addStretch(1);
    heroLayout->addLayout(heroButtons);

    // Arch news worth reading before updating, beside the hero.
    m_newsCard = M3::card(QStringLiteral("warn"), this);
    auto *newsLayout = new QVBoxLayout(m_newsCard);
    newsLayout->setContentsMargins(24, 22, 24, 22);
    newsLayout->setSpacing(10);
    m_newsKicker = M3::label(QString(), QStringLiteral("label"), m_newsCard);
    m_newsTitle = M3::label(QString(), QStringLiteral("titleM"), m_newsCard);
    m_newsTitle->setWordWrap(true);
    m_newsText = new QLabel(m_newsCard);
    m_newsText->setWordWrap(true);
    auto *newsButtons = new QHBoxLayout;
    auto *readButton = new QPushButton(tr("Read"), m_newsCard);
    MaterialTheme::styleButton(readButton, QStringLiteral("tonal"), QStringLiteral("campaign"));
    auto *dismissButton = new QPushButton(tr("Dismiss"), m_newsCard);
    MaterialTheme::styleButton(dismissButton, QStringLiteral("text"));
    newsButtons->addWidget(readButton);
    newsButtons->addWidget(dismissButton);
    newsButtons->addStretch(1);
    newsLayout->addWidget(m_newsKicker);
    newsLayout->addWidget(m_newsTitle);
    newsLayout->addWidget(m_newsText, 1);
    newsLayout->addLayout(newsButtons);
    m_newsCard->hide();

    auto *heroRow = new QHBoxLayout;
    heroRow->setSpacing(16);
    heroRow->addWidget(hero, 3);
    heroRow->addWidget(m_newsCard, 2);
    layout->addLayout(heroRow);

    auto tile = [this](const QString &icon, const QString &title, QLabel *&metric, QLabel *&text, QPushButton *&button,
                       const QString &buttonText) {
        QFrame *card = M3::card(QStringLiteral("card"), this);
        auto *l = new QVBoxLayout(card);
        l->setContentsMargins(20, 20, 20, 20);
        l->setSpacing(12);
        auto *head = new QHBoxLayout;
        auto *iconLabel = new QLabel(card);
        auto paintIcon = [iconLabel, icon]() {
            iconLabel->setPixmap(MaterialTheme::icon(icon, MaterialTheme::current().primary, 22).pixmap(22, 22));
        };
        paintIcon();
        connect(MaterialTheme::notifier(), &MaterialTheme::Notifier::changed, iconLabel, paintIcon);
        head->addWidget(iconLabel);
        head->addWidget(M3::label(title, QStringLiteral("titleM"), card), 1);
        l->addLayout(head);
        metric = M3::label(QStringLiteral("…"), QStringLiteral("metric"), card);
        l->addWidget(metric);
        text = M3::label(QString(), QStringLiteral("muted"), card);
        text->setWordWrap(true);
        l->addWidget(text, 1);
        button = new QPushButton(buttonText, card);
        MaterialTheme::styleButton(button, QStringLiteral("tonal"));
        l->addWidget(button, 0, Qt::AlignLeft);
        return card;
    };
    auto *tiles = new QHBoxLayout;
    tiles->setSpacing(16);
    QLabel *cacheText = nullptr;
    QPushButton *cleanButton = nullptr;
    QPushButton *installedButton = nullptr;
    tiles->addWidget(tile(QStringLiteral("cleaning_services"), tr("Cleanup"), m_cacheMetric, cacheText, cleanButton, tr("Free up space…")), 1);
    cacheText->setText(tr("in the package cache. Clear old package versions and dependencies nothing needs anymore."));
    tiles->addWidget(tile(QStringLiteral("apps"), tr("Installed"), m_installedMetric, m_installedText, installedButton,
                          tr("Browse installed")), 1);
    const bool hasAur = m_hub->indexOf(QStringLiteral("aur")) >= 0;
    QLabel *unusedText = nullptr;
    tiles->addWidget(tile(hasAur ? QStringLiteral("verified_user") : QStringLiteral("explore"),
                          hasAur ? tr("AUR safety") : tr("Discover"), m_aurMetric, hasAur ? m_aurText : unusedText, m_aurButton,
                          hasAur ? tr("Review AUR updates") : tr("Find apps")), 1);
    if (!hasAur) {
        m_aurText = unusedText;
        m_aurMetric->setText(QString::number(m_hub->sources().size()));
        m_aurText->setText(tr("sources searched at once: apps and packages from every one of them in a single list."));
    }
    // Snapshots, when Snapper or Timeshift is set up.
    const SystemTools::SnapshotTool snapshotTool = SystemTools::snapshotTool();
    if (snapshotTool != SystemTools::SnapshotTool::None) {
        QPushButton *settingsButton = nullptr;
        tiles->addWidget(tile(QStringLiteral("history"), tr("Snapshots"), m_snapshotMetric, m_snapshotText, settingsButton,
                              tr("Snapshot settings")),
                         1);
        connect(settingsButton, &QPushButton::clicked, this, [this]() { emit navigate(QStringLiteral("settings")); });
        m_snapshotText->setText(AppSettings::instance().snapshotBeforeTransactions()
                                    ? tr("%1 takes one before every system change.").arg(SystemTools::snapshotToolName(snapshotTool))
                                    : tr("%1 is set up. Turn on snapshots before transactions in Settings → System.")
                                          .arg(SystemTools::snapshotToolName(snapshotTool)));
    }
    layout->addLayout(tiles);

    // Recent activity, across sources.
    QFrame *activity = M3::card(QStringLiteral("card"), this);
    auto *activityLayout = new QVBoxLayout(activity);
    activityLayout->setContentsMargins(20, 20, 20, 16);
    activityLayout->setSpacing(6);
    activityLayout->addWidget(M3::label(tr("Recent activity"), QStringLiteral("titleM"), activity));
    m_activityRows = new QVBoxLayout;
    m_activityRows->setSpacing(0);
    m_activityRows->addWidget(M3::label(tr("Loading…"), QStringLiteral("muted"), activity));
    activityLayout->addLayout(m_activityRows);
    layout->addWidget(activity);
    layout->addStretch(1);

    connect(m_updateAllButton, &QPushButton::clicked, this, &HomePage::updateEverything);
    connect(m_seeUpdatesButton, &QPushButton::clicked, this, [this]() { emit navigate(QStringLiteral("updates")); });
    connect(installedButton, &QPushButton::clicked, this, [this]() { emit navigate(QStringLiteral("installed")); });
    connect(m_aurButton, &QPushButton::clicked, this, [this, hasAur]() {
        if (hasAur)
            emit reviewAurUpdates();
        else
            emit navigate(QStringLiteral("discover"));
    });
    connect(cleanButton, &QPushButton::clicked, this, [this]() {
        CleanupDialog dialog(m_nativeKey, m_native, window());
        if (dialog.runModal()) {
            m_hub->refreshInstalled();
            loadSideStats();
        }
    });
    cleanButton->setEnabled(m_native != nullptr);
    connect(readButton, &QPushButton::clicked, this, [this]() {
        const QVector<SystemTools::NewsItem> unread = unreadNews();
        if (unread.isEmpty())
            return;
        NewsDialog dialog(unread.first(), window());
        dialog.exec();
        if (unread.size() == 1)
            markNewsRead();
    });
    connect(dismissButton, &QPushButton::clicked, this, &HomePage::markNewsRead);
    connect(m_hub, &CatalogHub::updatesChanged, this, &HomePage::refreshUpdates);
    connect(m_hub, &CatalogHub::installedChanged, this, &HomePage::refreshInstalled);
    connect(MaterialTheme::notifier(), &MaterialTheme::Notifier::changed, this, &HomePage::refreshUpdates);
    connect(&m_statsWatcher, &QFutureWatcher<SideStats>::finished, this, [this]() {
        const SideStats stats = m_statsWatcher.result();
        m_cacheMetric->setText(stats.cacheBytes >= 0 ? QLocale().formattedDataSize(stats.cacheBytes) : tr("—"));
        showActivity(stats.activity);
        if (m_snapshotMetric) {
            m_snapshotMetric->setText(stats.snapshots.isEmpty() ? tr("—") : tr("#%1").arg(stats.snapshots.first().id));
            if (!stats.snapshots.isEmpty())
                m_snapshotText->setText(tr("Latest: %1, %2.").arg(stats.snapshots.first().description,
                                                                  QLocale().toString(stats.snapshots.first().time, QLocale::ShortFormat)));
        }
    });

    // News only matters on Arch.
    if (m_nativeKey == QLatin1String("pacman")) {
        connect(&m_newsWatcher, &QFutureWatcher<QPair<QVector<SystemTools::NewsItem>, QDateTime>>::finished, this, [this]() {
            const auto result = m_newsWatcher.result();
            m_news = result.first;
            m_lastUpgrade = result.second;
            showNews();
        });
        m_newsWatcher.setFuture(QtConcurrent::run([]() { return qMakePair(SystemTools::archNews(), SystemTools::lastSystemUpgrade()); }));
    }

    refreshUpdates();
    refreshInstalled();
    loadSideStats();
}

QVector<SystemTools::NewsItem> HomePage::unreadNews() const
{
    const QDateTime seen = AppSettings::instance().lastSeenNews();
    QVector<SystemTools::NewsItem> unread;
    for (const SystemTools::NewsItem &item : m_news) {
        if ((!m_lastUpgrade.isValid() || item.published > m_lastUpgrade) && (!seen.isValid() || item.published > seen))
            unread.append(item);
    }
    return unread;
}

void HomePage::markNewsRead()
{
    if (!m_news.isEmpty())
        AppSettings::instance().setLastSeenNews(m_news.first().published);
    showNews();
}

void HomePage::showNews()
{
    const QVector<SystemTools::NewsItem> unread = unreadNews();
    m_newsCard->setVisible(!unread.isEmpty());
    if (unread.isEmpty())
        return;
    const SystemTools::NewsItem &item = unread.first();
    m_newsKicker->setText(item.manualIntervention ? tr("ARCH NEWS · MANUAL INTERVENTION · READ BEFORE UPDATING")
                                                  : tr("ARCH NEWS · SINCE YOUR LAST UPDATE"));
    m_newsTitle->setText(item.title);
    // A plain-text taste of the post.
    QTextDocument doc;
    doc.setHtml(item.html);
    QString text = doc.toPlainText().simplified();
    if (text.size() > 220)
        text = text.left(220) + QStringLiteral("…");
    m_newsText->setText(unread.size() > 1 ? tr("%1\n\n%n more post(s) since your last update.", "", unread.size() - 1).arg(text)
                                          : text);
}

void HomePage::showActivity(const QVector<SystemTools::Activity> &activity)
{
    while (QLayoutItem *item = m_activityRows->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    if (activity.isEmpty()) {
        m_activityRows->addWidget(M3::label(tr("No recent activity recorded."), QStringLiteral("muted"), this));
        return;
    }
    for (int i = 0; i < activity.size() && i < 8; ++i)
        m_activityRows->addWidget(M3::activityRow(activity[i], this));
}

void HomePage::refreshUpdates()
{
    if (m_hub->updatesLoading() && m_hub->updates().isEmpty())
        return;
    const QVector<CatalogItem> &updates = m_hub->updates();
    QVector<int> perSource(m_hub->sources().size(), 0);
    for (const CatalogItem &item : updates)
        ++perSource[item.variants.first().source];

    QVector<QPair<int, QColor>> segments;
    QStringList legend;
    int sourcesWithUpdates = 0;
    const int aur = m_hub->indexOf(QStringLiteral("aur"));
    for (int i = 0; i < perSource.size(); ++i) {
        if (perSource[i] == 0)
            continue;
        const QColor color = segmentColor(sourcesWithUpdates);
        segments.append({perSource[i], color});
        legend << QStringLiteral("<span style=\"color:%1\">●</span> %2 %3%4")
                      .arg(color.name(QColor::HexArgb), m_hub->sources()[i].label.toHtmlEscaped())
                      .arg(perSource[i])
                      .arg(i == aur ? tr(" · needs review") : QString());
        ++sourcesWithUpdates;
    }
    m_updateBar->setSegments(segments);
    m_updateLegend->setText(legend.join(QStringLiteral("&nbsp;&nbsp;&nbsp;&nbsp;")));
    m_updateCount->setText(QString::number(updates.size()));
    m_updateSummary->setText(updates.isEmpty() ? tr("Everything is up to date.")
                                               : tr("updates across %n source(s)", "", sourcesWithUpdates));
    m_updateAllButton->setVisible(!updates.isEmpty());
    m_seeUpdatesButton->setText(updates.isEmpty() ? tr("Open Updates") : tr("See what's changing"));

    if (aur >= 0) {
        const int aurCount = perSource.value(aur);
        m_aurMetric->setText(QString::number(aurCount));
        m_aurText->setText(aurCount == 0 ? tr("No AUR updates waiting. Every AUR build opens for PKGBUILD review first.")
                                         : tr("AUR update(s) waiting. Each opens for PKGBUILD review before it builds.", "",
                                              aurCount));
        m_aurButton->setEnabled(aurCount > 0);
    }
}

void HomePage::refreshInstalled()
{
    const QVector<CatalogItem> &installed = m_hub->installed();
    int apps = 0;
    QStringList sourceNames;
    QVector<int> perSource(m_hub->sources().size(), 0);
    for (const CatalogItem &item : installed) {
        apps += item.isApp ? 1 : 0;
        for (const CatalogVariant &v : item.variants)
            ++perSource[v.source];
    }
    for (int i = 0; i < perSource.size(); ++i) {
        if (perSource[i] > 0)
            sourceNames << QStringLiteral("%1 %2").arg(QLocale().toString(perSource[i]), m_hub->sources()[i].label);
    }
    m_installedMetric->setText(QLocale().toString(apps));
    m_installedText->setText(tr("desktop apps, among %1.").arg(sourceNames.join(QStringLiteral(", "))));
}

void HomePage::loadSideStats()
{
    const QString nativeKey = m_nativeKey;
    const bool flatpak = m_hub->indexOf(QStringLiteral("flatpak")) >= 0;
    const bool snapshots = m_snapshotMetric != nullptr;
    m_statsWatcher.setFuture(QtConcurrent::run([nativeKey, flatpak, snapshots]() {
        SideStats stats;
        const QString cacheDir = nativeKey.isEmpty() ? QString() : SystemTools::packageCacheDir(nativeKey);
        if (!cacheDir.isEmpty() && QFileInfo::exists(cacheDir))
            stats.cacheBytes = SystemTools::directorySize(cacheDir);
        stats.activity = SystemTools::recentActivity(nativeKey, flatpak, 8);
        if (snapshots)
            stats.snapshots = SystemTools::listSnapshots(1);
        return stats;
    }));
}

// ---------------------------------------------------------------- Discover

DiscoverPage::DiscoverPage(CatalogHub *hub, DetailsSheet *sheet, QWidget *parent) : QWidget(parent), m_hub(hub), m_sheet(sheet)
{
    m_search = new QLineEdit(this);
    m_search->setProperty("m3", QStringLiteral("search"));
    m_search->setPlaceholderText(tr("Search apps and packages in every source"));
    m_search->setClearButtonEnabled(true);
    m_search->setMaximumWidth(760);
    QAction *searchIcon = m_search->addAction(QIcon(), QLineEdit::LeadingPosition);
    auto paintSearchIcon = [searchIcon]() {
        searchIcon->setIcon(MaterialTheme::icon(QStringLiteral("search"), MaterialTheme::current().onSurfaceVariant, 22));
    };
    paintSearchIcon();
    connect(MaterialTheme::notifier(), &MaterialTheme::Notifier::changed, this, paintSearchIcon);

    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(8);
    QVector<QPair<QString, QString>> chips = {{tr("All sources"), QString()}};
    for (const CatalogSource &source : hub->sources())
        chips.append({source.label, source.icon});
    m_sourceChips = M3::chipRow(chips, this, toolbar);
    toolbar->addSpacing(8);
    m_advancedToggle = new QPushButton(tr("Advanced"), this);
    m_advancedToggle->setCheckable(true);
    MaterialTheme::styleButton(m_advancedToggle, QStringLiteral("chip"), QStringLiteral("tune"));
    toolbar->addWidget(m_advancedToggle);
    m_selectToggle = new QPushButton(tr("Select"), this);
    m_selectToggle->setCheckable(true);
    MaterialTheme::styleButton(m_selectToggle, QStringLiteral("chip"), QStringLiteral("checklist"));
    toolbar->addWidget(m_selectToggle);
    toolbar->addStretch(1);
    m_mode = M3::segmented({{tr("Apps"), QStringLiteral("grid_view")}, {tr("Packages"), QStringLiteral("list")}}, this, toolbar);

    // Advanced: the classic search's Dependency Query and repository /
    // architecture filters.
    m_advancedRow = M3::card(QStringLiteral("card"), this);
    auto *advancedLayout = new QHBoxLayout(m_advancedRow);
    advancedLayout->setContentsMargins(16, 10, 16, 10);
    advancedLayout->setSpacing(10);
    m_dependencyCheck = new QCheckBox(tr("Dependency Query"), m_advancedRow);
    m_dependencyCheck->setToolTip(tr("Find packages that provide or require a name or capability (e.g. a library soname) "
                                     "instead of searching by name."));
    m_dependencyDirection = new QComboBox(m_advancedRow);
    m_dependencyDirection->addItems({tr("Provides"), tr("Requires")});
    m_dependencyDirection->setEnabled(false);
    m_repositoryFilter = new QComboBox(m_advancedRow);
    m_repositoryFilter->addItem(tr("All repositories"));
    m_architectureFilter = new QComboBox(m_advancedRow);
    m_architectureFilter->addItem(tr("All architectures"));
    advancedLayout->addWidget(m_dependencyCheck);
    advancedLayout->addWidget(m_dependencyDirection);
    advancedLayout->addSpacing(12);
    advancedLayout->addWidget(new QLabel(tr("Repository"), m_advancedRow));
    advancedLayout->addWidget(m_repositoryFilter, 1);
    advancedLayout->addWidget(new QLabel(tr("Architecture"), m_advancedRow));
    advancedLayout->addWidget(m_architectureFilter, 1);
    m_advancedRow->hide();

    QPushButton *selectAll = nullptr;
    QPushButton *done = nullptr;
    m_selectionBar = selectionBar(this, m_selectionCount, selectAll, m_installSelected, done, tr("Install selected"),
                                  QStringLiteral("filled"), QStringLiteral("download"));
    m_downloadSelected = new QPushButton(tr("Download"), m_selectionBar);
    MaterialTheme::styleButton(m_downloadSelected, QStringLiteral("tonal"), QStringLiteral("download"));
    static_cast<QHBoxLayout *>(m_selectionBar->layout())->insertWidget(2, m_downloadSelected);

    m_categoryRow = new QWidget(this);
    auto *categoryLayout = new QHBoxLayout(m_categoryRow);
    categoryLayout->setContentsMargins(0, 0, 0, 0);
    categoryLayout->setSpacing(8);
    QVector<QPair<QString, QString>> categories = {{tr("Your apps"), QStringLiteral("star")}};
    for (const Category &category : kCategories)
        categories.append({tr(category.label).replace('&', QStringLiteral("&&")), QString::fromLatin1(category.icon)});
    PackageBackend *native = hub->nativeSource() >= 0 ? hub->sources()[hub->nativeSource()].backend : nullptr;
    if (native)
        categories.append({tr("Package groups"), QStringLiteral("inventory_2")});
    m_categoryChips = M3::chipRow(categories, m_categoryRow, categoryLayout);
    if (native)
        m_groupsChip = int(categories.size()) - 1;
    m_groupPicker = new QComboBox(m_categoryRow);
    m_groupPicker->setMinimumWidth(220);
    m_groupPicker->hide();
    categoryLayout->addWidget(m_groupPicker);
    categoryLayout->addStretch(1);
    // More chips than fit in a narrow window: scroll them sideways rather
    // than squeezing their labels.
    auto *categoryScroll = new QScrollArea(this);
    categoryScroll->setFrameShape(QFrame::NoFrame);
    categoryScroll->setWidgetResizable(true);
    categoryScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    categoryScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    categoryScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    categoryScroll->viewport()->setAutoFillBackground(false);
    m_categoryRow->setAutoFillBackground(false);
    categoryScroll->setWidget(m_categoryRow);
    categoryScroll->setFixedHeight(m_categoryRow->sizeHint().height() + categoryScroll->horizontalScrollBar()->sizeHint().height() + 4);
    m_categoryScroll = categoryScroll;

    m_heading = M3::label(QString(), QStringLiteral("muted"), this);

    m_cardModel = new CatalogModel(this);
    m_rowModel = new CatalogModel(this);
    auto *cardDelegate = new CatalogCardDelegate(hub->sourcesPtr(), this);
    m_cards = catalogList(m_cardModel, cardDelegate, this);
    m_cards->setViewMode(QListView::IconMode);
    m_cards->setResizeMode(QListView::Adjust);
    m_cards->setMovement(QListView::Static);
    m_cards->setGridSize(QSize(CatalogCardDelegate::kCardWidth + 12, CatalogCardDelegate::kCardHeight + 12));
    m_cards->setSpacing(0);
    new GridFitter(m_cards, cardDelegate);
    m_cards->setWrapping(true);
    m_rows = catalogList(m_rowModel, new CatalogRowDelegate(hub->sourcesPtr(), CatalogRowDelegate::Kind::Package, this), this);
    openOnActivate(m_cards, m_cardModel, sheet);
    openOnActivate(m_rows, m_rowModel, sheet);
    for (const auto &[view, model] : {std::pair{m_cards, m_cardModel}, std::pair{m_rows, m_rowModel}}) {
        CatalogModel *forModel = model;
        CatalogActions::attachContextMenu(view, model, hub, sheet, [this, forModel](int row) {
            m_selectToggle->setChecked(true);
            if (!forModel->isSelected(row))
                forModel->toggleSelected(row);
        });
        connect(model, &CatalogModel::selectionChanged, this, &DiscoverPage::updateSelectionBar);
    }

    m_views = new QStackedWidget(this);
    m_views->addWidget(m_cards);
    m_views->addWidget(m_rows);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 4, 18, 0);
    layout->setSpacing(16);
    layout->addWidget(m_search);
    layout->addLayout(toolbar);
    layout->addWidget(m_advancedRow);
    layout->addWidget(m_selectionBar);
    layout->addWidget(m_categoryScroll);
    layout->addWidget(m_heading);
    layout->addWidget(m_views, 1);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(450);
    connect(m_debounce, &QTimer::timeout, this, &DiscoverPage::runSearch);
    connect(m_search, &QLineEdit::textChanged, m_debounce, qOverload<>(&QTimer::start));
    connect(m_search, &QLineEdit::returnPressed, this, &DiscoverPage::runSearch);
    connect(m_sourceChips, &QButtonGroup::idClicked, this, &DiscoverPage::showResults);
    connect(m_mode, &QButtonGroup::idClicked, this, &DiscoverPage::showResults);
    connect(m_categoryChips, &QButtonGroup::idClicked, this, [this](int id) {
        m_groupPicker->setVisible(id == m_groupsChip);
        if (id == m_groupsChip) {
            m_mode->button(1)->click(); // a group's members are packages, listed
            loadGroups();
        }
        showResults();
    });
    connect(m_groupPicker, &QComboBox::activated, this, &DiscoverPage::loadGroup);

    auto rerun = [this]() {
        m_activeQuery.clear(); // same text, different kind of search
        runSearch();
    };
    connect(m_advancedToggle, &QPushButton::toggled, this, [this, rerun](bool on) {
        m_advancedRow->setVisible(on);
        if (!on && m_dependencyCheck->isChecked())
            m_dependencyCheck->setChecked(false); // re-runs as a normal search
        else
            rerun(); // results with full details (architecture, repository) for the filters
    });
    connect(m_dependencyCheck, &QCheckBox::toggled, this, [this, rerun](bool on) {
        m_dependencyDirection->setEnabled(on);
        m_search->setPlaceholderText(on ? tr("A package name or capability, e.g. a library soname")
                                        : tr("Search apps and packages in every source"));
        if (on)
            m_mode->button(1)->click(); // dependency results are packages, not apps
        rerun();
    });
    connect(m_dependencyDirection, &QComboBox::currentIndexChanged, this, [this, rerun]() {
        if (m_dependencyCheck->isChecked())
            rerun();
    });
    connect(m_repositoryFilter, &QComboBox::currentIndexChanged, this, &DiscoverPage::showResults);
    connect(m_architectureFilter, &QComboBox::currentIndexChanged, this, &DiscoverPage::showResults);

    connect(m_selectToggle, &QPushButton::toggled, this, [this](bool on) {
        m_cardModel->setSelectionMode(on);
        m_rowModel->setSelectionMode(on);
        m_selectionBar->setVisible(on);
        updateSelectionBar();
    });
    connect(done, &QPushButton::clicked, m_selectToggle, [this]() { m_selectToggle->setChecked(false); });
    connect(selectAll, &QPushButton::clicked, this, [this]() { currentModel()->selectAll(); });
    connect(m_installSelected, &QPushButton::clicked, this, [this]() {
        // Installing applies to what isn't installed yet, from each item's first source.
        QVector<CatalogVariant> variants;
        for (const CatalogItem &item : currentModel()->selectedItems()) {
            if (!item.installed())
                variants.append(item.variants[item.preferredVariant()]);
        }
        CatalogActions::run(this, m_hub->sources(), variants, CatalogActions::Action::Install, [this](bool) {
            m_selectToggle->setChecked(false);
            m_hub->refreshAll();
        });
    });
    connect(m_downloadSelected, &QPushButton::clicked, this, [this]() {
        QMenu menu(m_downloadSelected);
        menu.addAction(tr("Download selected"));
        QAction *withDeps = menu.addAction(tr("Download selected + dependencies"));
        QAction *chosen = menu.exec(m_downloadSelected->mapToGlobal(QPoint(0, m_downloadSelected->height())));
        if (!chosen)
            return;
        CatalogActions::download(this, m_hub->sources(), CatalogActions::preferredVariants(currentModel()->selectedItems()),
                                 chosen == withDeps, [](bool) {});
    });

    auto relayoutRows = [this]() { m_rows->doItemsLayout(); m_rows->viewport()->update(); };
    connect(&AppSettings::instance(), &AppSettings::compactPackageListsChanged, this, relayoutRows);
    connect(&AppSettings::instance(), &AppSettings::packageListColumnsChanged, this, relayoutRows);
    connect(m_hub, &CatalogHub::installedChanged, this, &DiscoverPage::showResults);
    connect(m_hub->appIndex(), &AppStreamIndex::loaded, this, [this]() {
        if (m_activeQuery.isEmpty()) {
            showResults();
        } else {
            m_activeQuery.clear(); // force a re-run that includes the index
            runSearch();
        }
    });
    showResults();
}

void DiscoverPage::focusSearch()
{
    m_search->setFocus();
    m_search->selectAll();
}

void DiscoverPage::runSearch()
{
    m_debounce->stop();
    const QString query = m_search->text().trimmed();
    if (query == m_activeQuery && m_pending == 0)
        return;
    m_activeQuery = query;
    m_results.clear();
    const int generation = ++m_generation;
    if (query.isEmpty()) {
        m_pending = 0;
        showResults();
        return;
    }

    // The index answers instantly; each backend's own search (which also
    // finds non-app packages, and the AUR) streams in after. A dependency
    // query has no index equivalent: it's backends only.
    const bool dependencyQuery = m_dependencyCheck->isChecked();
    const bool findRequires = m_dependencyDirection->currentIndex() == 1;
    const bool detailed = m_advancedToggle->isChecked();
    m_results = dependencyQuery ? QVector<CatalogItem>() : m_hub->itemsFor(m_hub->appIndex()->search(query));
    const QVector<CatalogSource> &sources = m_hub->sources();
    m_pending = sources.size();
    showResults();
    for (int i = 0; i < sources.size(); ++i) {
        auto *watcher = new QFutureWatcher<QVector<PackageInfo>>(this);
        connect(watcher, &QFutureWatcher<QVector<PackageInfo>>::finished, this, [this, watcher, i, generation]() {
            const QVector<PackageInfo> found = watcher->result();
            watcher->deleteLater();
            if (generation != m_generation)
                return;
            for (const PackageInfo &pkg : found)
                m_results.append(Catalog::fromPackage(m_hub->sources(), i, pkg));
            --m_pending;
            showResults(); // results stream in source by source
        });
        PackageBackend *backend = sources[i].backend;
        watcher->setFuture(QtConcurrent::run([backend, query, dependencyQuery, findRequires, detailed]() {
            if (dependencyQuery)
                return backend->dependencyQuery(query, findRequires);
            const QVector<PackageInfo> found = backend->search(query);
            if (!detailed)
                return found;
            // With Advanced open, fetch full details (architecture,
            // repository) like the classic search did — capped, since it's
            // one more query per result.
            QStringList names;
            for (const PackageInfo &pkg : found) {
                if (names.size() < 200)
                    names << pkg.name;
            }
            return backend->packageDetails(names);
        }));
    }
}

void DiscoverPage::showResults()
{
    const int source = m_sourceChips->checkedId() - 1; // chip 0 is "All sources"
    const bool appsMode = m_mode->checkedId() == 0;
    const bool groupsMode = m_activeQuery.isEmpty() && m_categoryChips->checkedId() == m_groupsChip;
    // chip 0 is "Your apps"; the categories follow; the groups chip is last.
    const int category = m_activeQuery.isEmpty() && !groupsMode ? m_categoryChips->checkedId() - 1 : -1;
    Motion::switchStack(m_views, appsMode ? 0 : 1);
    m_categoryScroll->setVisible(m_activeQuery.isEmpty());

    // Mark results the user already has (not every backend's search says).
    QSet<QString> installedKeys;
    for (const CatalogItem &item : m_hub->installed()) {
        for (const CatalogVariant &v : item.variants)
            installedKeys.insert(QString::number(v.source) + '/' + v.info.name);
    }

    populateFilters();
    QVector<CatalogItem> base;
    if (groupsMode) {
        base = m_groupItems;
        for (CatalogItem &item : base) {
            for (CatalogVariant &v : item.variants) {
                if (installedKeys.contains(QString::number(v.source) + '/' + v.info.name))
                    v.info.installed = true;
            }
        }
    } else if (m_activeQuery.isEmpty() && category < 0) {
        for (const CatalogItem &item : m_hub->installed()) {
            if (item.isApp)
                base.append(item);
        }
    } else {
        base = m_activeQuery.isEmpty() ? m_hub->itemsFor(m_hub->appIndex()->inCategory(QString::fromLatin1(kCategories[category].key)))
                                       : m_results;
        for (CatalogItem &item : base) {
            for (CatalogVariant &v : item.variants) {
                if (installedKeys.contains(QString::number(v.source) + '/' + v.info.name))
                    v.info.installed = true;
            }
        }
    }

    // The AUR has no app catalog, so its search results never look like
    // apps on their own. Strong matches (the name is, or starts with, the
    // search) show as cards anyway, and with the AUR chip selected, all of
    // its results do.
    const int aurSource = m_hub->indexOf(QStringLiteral("aur"));
    if (appsMode && aurSource >= 0 && !m_activeQuery.isEmpty()) {
        for (CatalogItem &item : base) {
            if (!item.isApp && itemFromSource(item, aurSource) && (source == aurSource || relevance(item, m_activeQuery) <= 1))
                item.isApp = true;
        }
    }

    // Advanced filters: keep items with a variant from that repository /
    // for that architecture.
    const QString repository = m_advancedToggle->isChecked() && m_repositoryFilter->currentIndex() > 0 ? m_repositoryFilter->currentText()
                                                                                                       : QString();
    const QString architecture =
        m_advancedToggle->isChecked() && m_architectureFilter->currentIndex() > 0 ? m_architectureFilter->currentText() : QString();
    if (!repository.isEmpty() || !architecture.isEmpty()) {
        QVector<CatalogItem> filtered;
        for (const CatalogItem &item : base) {
            const bool match = std::any_of(item.variants.begin(), item.variants.end(), [&](const CatalogVariant &v) {
                return (repository.isEmpty() || v.info.repository == repository)
                    && (architecture.isEmpty() || v.info.architecture == architecture);
            });
            if (match)
                filtered.append(item);
        }
        base = filtered;
    }

    QVector<CatalogItem> shown;
    const bool fromIndex = !m_activeQuery.isEmpty() || category >= 0;
    QVector<CatalogItem> candidates = appsMode && fromIndex ? Catalog::mergeApps(base) : base;
    if (category >= 0) {
        std::sort(candidates.begin(), candidates.end(), [](const CatalogItem &a, const CatalogItem &b) {
            return QString::compare(a.title, b.title, Qt::CaseInsensitive) < 0;
        });
    }
    for (const CatalogItem &item : candidates) {
        if (appsMode && !item.isApp)
            continue;
        if (source >= 0 && !itemFromSource(item, source))
            continue;
        shown.append(item);
    }
    if (!m_activeQuery.isEmpty()) {
        const QString query = m_activeQuery;
        std::stable_sort(shown.begin(), shown.end(), [&query](const CatalogItem &a, const CatalogItem &b) {
            return relevance(a, query) < relevance(b, query);
        });
    }
    (appsMode ? m_cardModel : m_rowModel)->setItems(shown);

    QString heading;
    const AppStreamIndex *index = m_hub->appIndex();
    if (groupsMode)
        heading = m_groupPicker->currentIndex() < 0 ? tr("Pick a package group")
                                                    : tr("%n package(s) in %1", "", shown.size()).arg(m_groupPicker->currentText());
    else if (m_activeQuery.isEmpty() && category < 0)
        heading = index->isLoaded()
            ? tr("Your apps · %1 apps from your sources are a search or a category away").arg(QLocale().toString(index->size()))
            : tr("Your apps · loading the app catalog…");
    else if (category >= 0)
        heading = index->isLoaded() ? tr("%n app(s) in %1", "", shown.size()).arg(tr(kCategories[category].label))
                                    : tr("Loading the app catalog…");
    else if (m_pending > 0)
        heading = tr("Searching %n more source(s)… %1 found so far", "", m_pending).arg(shown.size());
    else if (shown.isEmpty())
        heading = appsMode ? tr("No apps match \"%1\". Try Packages for libraries and command-line tools.").arg(m_activeQuery)
                           : tr("No packages match \"%1\".").arg(m_activeQuery);
    else
        heading = appsMode ? tr("%n app(s) for \"%1\"", "", shown.size()).arg(m_activeQuery)
                           : tr("%n package(s) for \"%1\"", "", shown.size()).arg(m_activeQuery);
    m_heading->setText(heading);
}

void DiscoverPage::loadGroups()
{
    if (m_groupsLoaded)
        return;
    m_groupsLoaded = true;
    PackageBackend *native = m_hub->sources()[m_hub->nativeSource()].backend;
    auto *watcher = new QFutureWatcher<QVector<PackageGroupInfo>>(this);
    connect(watcher, &QFutureWatcher<QVector<PackageGroupInfo>>::finished, this, [this, watcher]() {
        QVector<PackageGroupInfo> groups = watcher->result();
        watcher->deleteLater();
        std::sort(groups.begin(), groups.end(), [](const PackageGroupInfo &a, const PackageGroupInfo &b) {
            return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
        });
        const QSignalBlocker blocker(m_groupPicker);
        m_groupPicker->clear();
        for (const PackageGroupInfo &group : groups)
            m_groupPicker->addItem(group.name, QVariant::fromValue(QStringList{group.id, group.isMeta ? QStringLiteral("1") : QString()}));
        m_groupPicker->setCurrentIndex(-1);
        m_groupPicker->setPlaceholderText(groups.isEmpty() ? tr("No groups") : tr("Choose a group"));
        showResults();
    });
    watcher->setFuture(QtConcurrent::run([native]() { return native->listGroups(); }));
}

void DiscoverPage::loadGroup(int index)
{
    if (index < 0)
        return;
    const QStringList data = m_groupPicker->itemData(index).toStringList();
    const int source = m_hub->nativeSource();
    PackageBackend *native = m_hub->sources()[source].backend;
    const QString id = data.value(0);
    const bool isMeta = !data.value(1).isEmpty();
    m_groupItems.clear();
    showResults();
    auto *watcher = new QFutureWatcher<QVector<PackageInfo>>(this);
    connect(watcher, &QFutureWatcher<QVector<PackageInfo>>::finished, this, [this, watcher, source]() {
        for (const PackageInfo &pkg : watcher->result())
            m_groupItems.append(Catalog::fromPackage(m_hub->sources(), source, pkg));
        watcher->deleteLater();
        showResults();
    });
    // Same expansion as the classic Groups tab: environments/meta groups
    // expand into their member groups' packages.
    watcher->setFuture(QtConcurrent::run([native, id, isMeta]() {
        const PackageGroupInfo group = native->groupDetails(id, isMeta);
        QStringList names = group.packages;
        for (const QString &sub : group.subGroups) {
            for (const QString &name : native->groupDetails(sub, false).packages) {
                if (!names.contains(name))
                    names << name;
            }
        }
        return native->packageDetails(names);
    }));
}

void DiscoverPage::populateFilters()
{
    QSet<QString> repositories;
    QSet<QString> architectures;
    for (const CatalogItem &item : m_results) {
        for (const CatalogVariant &v : item.variants) {
            if (!v.info.repository.isEmpty() && v.info.repository != QLatin1String("installed"))
                repositories.insert(v.info.repository);
            if (!v.info.architecture.isEmpty())
                architectures.insert(v.info.architecture);
        }
    }
    auto refill = [](QComboBox *combo, const QString &all, const QSet<QString> &values) {
        const QString previous = combo->currentIndex() > 0 ? combo->currentText() : QString();
        QStringList sorted = values.values();
        sorted.sort(Qt::CaseInsensitive);
        const QSignalBlocker blocker(combo);
        combo->clear();
        combo->addItem(all);
        combo->addItems(sorted);
        const int index = previous.isEmpty() ? 0 : combo->findText(previous);
        combo->setCurrentIndex(index >= 0 ? index : 0);
    };
    refill(m_repositoryFilter, tr("All repositories"), repositories);
    refill(m_architectureFilter, tr("All architectures"), architectures);
}

CatalogModel *DiscoverPage::currentModel() const
{
    return m_mode->checkedId() == 0 ? m_cardModel : m_rowModel;
}

void DiscoverPage::updateSelectionBar()
{
    const QVector<CatalogItem> selected = currentModel()->selectedItems();
    const int installable = int(std::count_if(selected.begin(), selected.end(), [](const CatalogItem &i) { return !i.installed(); }));
    m_selectionCount->setText(tr("%n selected", "", selected.size()));
    m_installSelected->setEnabled(installable > 0);
    m_installSelected->setText(installable > 0 && installable != selected.size() ? tr("Install %n", "", installable)
                                                                                 : tr("Install selected"));
    m_downloadSelected->setEnabled(!selected.isEmpty());
}

// ---------------------------------------------------------------- Installed

LibraryPage::LibraryPage(CatalogHub *hub, DetailsSheet *sheet, QWidget *parent) : QWidget(parent), m_hub(hub), m_sheet(sheet)
{
    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(8);
    QVector<QPair<QString, QString>> chips = {{tr("All sources"), QString()}};
    for (const CatalogSource &source : hub->sources())
        chips.append({source.label, source.icon});
    m_sourceChips = M3::chipRow(chips, this, toolbar);
    toolbar->addSpacing(8);
    m_appsOnly = new QPushButton(tr("Apps only"), this);
    m_appsOnly->setCheckable(true);
    m_appsOnly->setChecked(true);
    MaterialTheme::styleButton(m_appsOnly, QStringLiteral("chip"), QStringLiteral("apps"));
    toolbar->addWidget(m_appsOnly);
    toolbar->addStretch(1);
    auto *refresh = new QPushButton(this);
    MaterialTheme::styleButton(refresh, QStringLiteral("icon"), QStringLiteral("refresh"));
    refresh->setToolTip(tr("Reload installed packages"));
    m_selectToggle = new QPushButton(tr("Select"), this);
    m_selectToggle->setCheckable(true);
    MaterialTheme::styleButton(m_selectToggle, QStringLiteral("chip"), QStringLiteral("checklist"));
    toolbar->insertWidget(toolbar->count() - 1, m_selectToggle);
    toolbar->addWidget(refresh);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Filter installed apps and packages"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setMaximumWidth(420);
    m_count = M3::label(tr("Loading installed packages…"), QStringLiteral("muted"), this);
    // Says what "Apps only" is hiding, with a way to see it.
    m_showAll = new QPushButton(tr("Show all"), this);
    MaterialTheme::styleButton(m_showAll, QStringLiteral("text"));
    m_showAll->hide();
    auto *filterRow = new QHBoxLayout;
    filterRow->addWidget(m_filter, 1);
    filterRow->addWidget(m_count);
    filterRow->addWidget(m_showAll);
    filterRow->addStretch(1);

    m_model = new CatalogModel(this);
    m_list = catalogList(m_model, new CatalogRowDelegate(hub->sourcesPtr(), CatalogRowDelegate::Kind::Installed, this), this);
    openOnActivate(m_list, m_model, sheet);
    CatalogActions::attachContextMenu(m_list, m_model, hub, sheet, [this](int row) {
        m_selectToggle->setChecked(true);
        if (!m_model->isSelected(row))
            m_model->toggleSelected(row);
    });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 4, 18, 0);
    layout->setSpacing(16);
    layout->addLayout(toolbar);
    layout->addLayout(filterRow);
    QPushButton *selectAll = nullptr;
    QPushButton *done = nullptr;
    m_selectionBar = selectionBar(this, m_selectionCount, selectAll, m_removeSelected, done, tr("Remove selected"),
                                  QStringLiteral("danger"), QStringLiteral("delete"));
    layout->addWidget(m_selectionBar);
    layout->addWidget(m_list, 1);

    connect(m_sourceChips, &QButtonGroup::idClicked, this, &LibraryPage::apply);
    connect(m_appsOnly, &QPushButton::toggled, this, &LibraryPage::apply);
    connect(m_showAll, &QPushButton::clicked, m_appsOnly, [this]() { m_appsOnly->setChecked(false); });
    connect(m_filter, &QLineEdit::textChanged, this, &LibraryPage::apply);
    connect(refresh, &QPushButton::clicked, m_hub, &CatalogHub::refreshInstalled);
    connect(m_hub, &CatalogHub::installedChanged, this, &LibraryPage::apply);
    connect(m_selectToggle, &QPushButton::toggled, this, [this](bool on) {
        m_model->setSelectionMode(on);
        m_selectionBar->setVisible(on);
    });
    connect(done, &QPushButton::clicked, m_selectToggle, [this]() { m_selectToggle->setChecked(false); });
    connect(selectAll, &QPushButton::clicked, m_model, &CatalogModel::selectAll);
    connect(m_model, &CatalogModel::selectionChanged, this, [this]() {
        const int n = m_model->selectedItems().size();
        m_selectionCount->setText(tr("%n selected", "", n));
        m_removeSelected->setEnabled(n > 0);
    });
    connect(m_removeSelected, &QPushButton::clicked, this, [this]() {
        CatalogActions::run(this, m_hub->sources(), CatalogActions::preferredVariants(m_model->selectedItems()),
                            CatalogActions::Action::Remove, [this](bool) {
            m_selectToggle->setChecked(false);
            m_hub->refreshAll();
        });
    });
    auto relayout = [this]() { m_list->doItemsLayout(); m_list->viewport()->update(); };
    connect(&AppSettings::instance(), &AppSettings::compactPackageListsChanged, this, relayout);
    connect(&AppSettings::instance(), &AppSettings::packageListColumnsChanged, this, relayout);
    apply();
}

void LibraryPage::apply()
{
    const int source = m_sourceChips->checkedId() - 1;
    const QString filter = m_filter->text().trimmed();
    QVector<CatalogItem> shown;
    int hidden = 0;
    for (const CatalogItem &item : m_hub->installed()) {
        if (m_appsOnly->isChecked() && !item.isApp) {
            if (source < 0 || itemFromSource(item, source))
                ++hidden;
            continue;
        }
        if (source >= 0 && !itemFromSource(item, source))
            continue;
        if (!filter.isEmpty() && !item.title.contains(filter, Qt::CaseInsensitive)
            && !item.variants.first().info.name.contains(filter, Qt::CaseInsensitive)
            && !item.subtitle.contains(filter, Qt::CaseInsensitive))
            continue;
        shown.append(item);
    }
    m_model->setItems(shown);
    if (m_hub->installedLoading() && m_hub->installed().isEmpty())
        m_count->setText(tr("Loading installed packages…"));
    else if (m_appsOnly->isChecked() && hidden > 0)
        m_count->setText(tr("%n app(s) · %1 more package(s) hidden", "", shown.size()).arg(QLocale().toString(hidden)));
    else
        m_count->setText(m_appsOnly->isChecked() ? tr("%n app(s)", "", shown.size()) : tr("%n package(s)", "", shown.size()));
    m_showAll->setVisible(m_appsOnly->isChecked() && hidden > 0);
}

// ---------------------------------------------------------------- Updates

AllUpdatesPage::AllUpdatesPage(CatalogHub *hub, DetailsSheet *sheet, QWidget *parent) : QWidget(parent), m_hub(hub), m_sheet(sheet)
{
    QVBoxLayout *layout = scrollingPage(this);

    auto *header = new QHBoxLayout;
    m_summary = M3::label(tr("Checking every source for updates…"), QStringLiteral("muted"), this);
    m_summary->setWordWrap(true);
    m_updateAll = new QPushButton(tr("Update everything"), this);
    MaterialTheme::styleButton(m_updateAll, QStringLiteral("filled"), QStringLiteral("system_update_alt"));
    auto *refresh = new QPushButton(this);
    MaterialTheme::styleButton(refresh, QStringLiteral("icon"), QStringLiteral("refresh"));
    refresh->setToolTip(tr("Check for updates again"));
    header->addWidget(m_summary, 1);
    header->addWidget(refresh);
    m_selectToggle = new QPushButton(tr("Select"), this);
    m_selectToggle->setCheckable(true);
    MaterialTheme::styleButton(m_selectToggle, QStringLiteral("chip"), QStringLiteral("checklist"));
    header->insertWidget(1, m_selectToggle);
    header->addWidget(m_updateAll);
    layout->addLayout(header);
    QPushButton *selectAll = nullptr;
    QPushButton *done = nullptr;
    m_selectionBar = selectionBar(this, m_selectionCount, selectAll, m_updateSelected, done, tr("Update selected"),
                                  QStringLiteral("filled"), QStringLiteral("system_update_alt"));
    layout->addWidget(m_selectionBar);
    connect(m_selectToggle, &QPushButton::toggled, this, [this](bool on) {
        for (CatalogModel *model : m_sectionModels)
            model->setSelectionMode(on);
        m_selectionBar->setVisible(on);
        updateSelectionBar();
    });
    connect(done, &QPushButton::clicked, m_selectToggle, [this]() { m_selectToggle->setChecked(false); });
    connect(selectAll, &QPushButton::clicked, this, [this]() {
        for (CatalogModel *model : m_sectionModels)
            model->selectAll();
    });
    connect(m_updateSelected, &QPushButton::clicked, this, [this]() {
        QVector<CatalogItem> selected;
        for (CatalogModel *model : m_sectionModels)
            selected += model->selectedItems();
        CatalogActions::run(this, m_hub->sources(), CatalogActions::preferredVariants(selected), CatalogActions::Action::Upgrade,
                            [this](bool) {
            m_selectToggle->setChecked(false);
            m_hub->refreshAll();
        });
    });
    connect(&AppSettings::instance(), &AppSettings::compactPackageListsChanged, this, &AllUpdatesPage::rebuild);
    connect(&AppSettings::instance(), &AppSettings::packageListColumnsChanged, this, &AllUpdatesPage::rebuild);

    m_empty = new QWidget(this);
    auto *emptyLayout = new QVBoxLayout(m_empty);
    emptyLayout->setContentsMargins(0, 48, 0, 48);
    auto *emptyIcon = new QLabel(m_empty);
    emptyIcon->setAlignment(Qt::AlignCenter);
    auto paintEmpty = [emptyIcon]() {
        emptyIcon->setPixmap(MaterialTheme::icon(QStringLiteral("check_circle"), MaterialTheme::current().primary, 56, true).pixmap(56, 56));
    };
    paintEmpty();
    connect(MaterialTheme::notifier(), &MaterialTheme::Notifier::changed, emptyIcon, paintEmpty);
    auto *emptyText = M3::label(tr("Everything is up to date"), QStringLiteral("title"), m_empty);
    emptyText->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyIcon);
    emptyLayout->addWidget(emptyText);
    m_empty->hide();
    layout->addWidget(m_empty);

    m_sections = new QVBoxLayout;
    m_sections->setSpacing(24);
    layout->addLayout(m_sections);
    layout->addStretch(1);

    connect(m_updateAll, &QPushButton::clicked, this, &AllUpdatesPage::updateEverything);
    connect(refresh, &QPushButton::clicked, m_hub, &CatalogHub::refreshUpdates);
    connect(m_hub, &CatalogHub::updatesChanged, this, &AllUpdatesPage::rebuild);
    rebuild();
}

void AllUpdatesPage::rebuild()
{
    while (QLayoutItem *item = m_sections->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    m_sectionModels.clear();

    const QVector<CatalogItem> &updates = m_hub->updates();
    if (m_hub->updatesLoading() && updates.isEmpty()) {
        m_summary->setText(tr("Checking every source for updates…"));
        m_updateAll->hide();
        m_empty->hide();
        return;
    }
    m_empty->setVisible(updates.isEmpty());
    m_updateAll->setVisible(!updates.isEmpty());
    const int aur = m_hub->indexOf(QStringLiteral("aur"));
    m_summary->setText(updates.isEmpty()
                           ? tr("Every source is up to date.")
                           : tr("%n update(s). AUR updates open for PKGBUILD review after the rest finish.", "", updates.size()));

    for (int s = 0; s < m_hub->sources().size(); ++s) {
        QVector<CatalogItem> items;
        for (const CatalogItem &item : updates) {
            if (item.variants.first().source == s)
                items.append(item);
        }
        if (items.isEmpty())
            continue;
        const CatalogSource &source = m_hub->sources()[s];

        auto *section = new QWidget(this);
        auto *sectionLayout = new QVBoxLayout(section);
        sectionLayout->setContentsMargins(0, 0, 0, 0);
        sectionLayout->setSpacing(10);
        auto *head = new QHBoxLayout;
        head->addWidget(M3::label(source.label, QStringLiteral("titleM"), section));
        head->addWidget(M3::label(tr("%n update(s)", "", items.size()), QStringLiteral("muted"), section));
        head->addStretch(1);
        auto *groupButton = new QPushButton(s == aur ? tr("Review & update all") : tr("Update all"), section);
        MaterialTheme::styleButton(groupButton, QStringLiteral("tonal"),
                                   s == aur ? QStringLiteral("verified_user") : QStringLiteral("system_update_alt"));
        head->addWidget(groupButton);
        sectionLayout->addLayout(head);

        auto *model = new CatalogModel(section);
        model->setItems(items);
        model->setSelectionMode(m_selectToggle->isChecked());
        m_sectionModels.append(model);
        connect(model, &CatalogModel::selectionChanged, this, &AllUpdatesPage::updateSelectionBar);
        QListView *list = catalogList(model, new CatalogRowDelegate(m_hub->sourcesPtr(), CatalogRowDelegate::Kind::Update, section),
                                      section);
        list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setFixedHeight(items.size() * CatalogRowDelegate::rowHeight() + 2);
        openOnActivate(list, model, m_sheet, true);
        CatalogActions::attachContextMenu(list, model, m_hub, m_sheet, [this, model](int row) {
            m_selectToggle->setChecked(true);
            if (!model->isSelected(row))
                model->toggleSelected(row);
        });
        sectionLayout->addWidget(list);
        m_sections->addWidget(section);

        QStringList names;
        for (const CatalogItem &item : items)
            names << item.variants.first().info.name;
        PackageBackend *backend = source.backend;
        connect(groupButton, &QPushButton::clicked, this, [this, backend, names]() {
            PackageActions::upgradePackages(this, backend, names, [this](bool success) {
                if (success) {
                    m_hub->refreshUpdates();
                    m_hub->refreshInstalled();
                }
            });
        });
    }
}

void AllUpdatesPage::updateSelectionBar()
{
    int n = 0;
    for (const CatalogModel *model : m_sectionModels)
        n += model->selectedItems().size();
    m_selectionCount->setText(tr("%n selected", "", n));
    m_updateSelected->setEnabled(n > 0);
}

// ---------------------------------------------------------------- Sources

SourcesPage::SourcesPage(QWidget *parent) : QWidget(parent)
{
    m_chipRow = new QHBoxLayout;
    m_chipRow->setSpacing(8);
    m_chips = new QButtonGroup(this);
    m_chips->setExclusive(true);
    m_stack = new QStackedWidget(this);

    auto *row = new QHBoxLayout;
    row->addLayout(m_chipRow);
    row->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 4, 18, 12);
    layout->setSpacing(12);
    layout->addLayout(row);
    layout->addWidget(m_stack, 1);

    connect(m_chips, &QButtonGroup::idClicked, this, [this](int index) { Motion::switchStack(m_stack, index); });
}

void SourcesPage::addSource(const QString &label, const QString &icon, QWidget *tabs)
{
    auto *chip = new QPushButton(label, this);
    chip->setCheckable(true);
    MaterialTheme::styleButton(chip, QStringLiteral("chip"), icon);
    const int index = m_stack->addWidget(tabs);
    m_chips->addButton(chip, index);
    m_chipRow->addWidget(chip);
    if (index == 0)
        chip->setChecked(true);
}

int SourcesPage::indexOfSource(const QString &label) const
{
    for (QAbstractButton *chip : m_chips->buttons()) {
        if (chip->text() == label)
            return m_chips->id(chip);
    }
    return -1;
}

void SourcesPage::showSource(int index)
{
    if (QAbstractButton *chip = m_chips->button(index)) {
        chip->setChecked(true);
        m_stack->setCurrentIndex(index);
    }
}
