#include "HistoryPage.h"

#include <QButtonGroup>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "AppIcons.h"
#include "M3Widgets.h"
#include "MaterialTheme.h"
#include "Motion.h"

HistoryPage::HistoryPage(PackageBackend *backend, QWidget *parent, const QString &activityKey)
    : QWidget(parent), m_backend(backend), m_activityKey(activityKey)
{
    m_refreshButton = new QPushButton(AppIcons::refresh(), tr("Refresh"), this);
    MaterialTheme::styleButton(m_refreshButton, QStringLiteral("tonal"), QStringLiteral("refresh"));
    m_statusLabel = new QLabel(tr("Loading history..."), this);
    m_statusLabel->setProperty("m3", QStringLiteral("muted"));

    m_view = new QPlainTextEdit(this);
    m_view->setReadOnly(true);
    m_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);

    auto *topRow = new QHBoxLayout;
    topRow->addWidget(m_statusLabel, 1);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(topRow);

    if (!m_activityKey.isEmpty()) {
        // Timeline | Raw log.
        QButtonGroup *mode = M3::segmented({{tr("Timeline"), QStringLiteral("history")}, {tr("Raw log"), QStringLiteral("terminal")}},
                                           this, topRow);
        auto *card = new QFrame(this);
        card->setProperty("m3", QStringLiteral("card"));
        m_timeline = new QVBoxLayout(card);
        m_timeline->setContentsMargins(20, 8, 20, 8);
        m_timeline->setSpacing(0);
        auto *scroll = new QScrollArea(this);
        scroll->setProperty("m3", QStringLiteral("page"));
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        auto *content = new QWidget(scroll);
        auto *contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->addWidget(card);
        contentLayout->addStretch(1);
        scroll->setWidget(content);
        m_views = new QStackedWidget(this);
        m_views->addWidget(scroll);
        m_views->addWidget(m_view);
        layout->addWidget(m_views, 1);
        connect(mode, &QButtonGroup::idClicked, this, [this](int id) { Motion::switchStack(m_views, id); });
    } else {
        layout->addWidget(m_view, 1);
    }
    topRow->addWidget(m_refreshButton);

    connect(m_refreshButton, &QPushButton::clicked, this, &HistoryPage::refresh);
    connect(&m_watcher, &QFutureWatcher<Loaded>::finished, this, &HistoryPage::onLoaded);

    refresh();
}

void HistoryPage::refresh()
{
    if (!m_backend)
        return;

    m_refreshButton->setEnabled(false);
    m_statusLabel->setText(tr("Loading history..."));

    PackageBackend *backend = m_backend;
    const QString key = m_activityKey;
    m_watcher.setFuture(QtConcurrent::run([backend, key]() {
        Loaded loaded;
        loaded.raw = backend->recentHistory();
        if (key == QLatin1String("flatpak"))
            loaded.activity = SystemTools::recentActivity(QString(), true, 200);
        else if (!key.isEmpty())
            loaded.activity = SystemTools::recentActivity(key, false, 200);
        return loaded;
    }));
}

void HistoryPage::onLoaded()
{
    const Loaded loaded = m_watcher.result();
    m_view->setPlainText(loaded.raw.isEmpty() ? tr("No history available.") : loaded.raw);
    if (m_timeline) {
        while (QLayoutItem *item = m_timeline->takeAt(0)) {
            if (QWidget *widget = item->widget())
                widget->deleteLater();
            delete item;
        }
        if (loaded.activity.isEmpty())
            m_timeline->addWidget(M3::label(tr("No activity recorded."), QStringLiteral("muted"), this));
        for (const SystemTools::Activity &activity : loaded.activity)
            m_timeline->addWidget(M3::activityRow(activity, this));
        m_statusLabel->setText(tr("%n recent change(s)", "", loaded.activity.size()));
    } else {
        m_statusLabel->setText(tr("Recent activity"));
    }
    m_refreshButton->setEnabled(true);
}
