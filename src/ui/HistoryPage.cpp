#include "HistoryPage.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "AppIcons.h"

HistoryPage::HistoryPage(PackageBackend *backend, QWidget *parent) : QWidget(parent), m_backend(backend)
{
    m_refreshButton = new QPushButton(AppIcons::refresh(), tr("Refresh"), this);
    m_statusLabel = new QLabel(tr("Loading history..."), this);

    m_view = new QPlainTextEdit(this);
    m_view->setReadOnly(true);
    m_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);

    auto *topRow = new QHBoxLayout;
    topRow->addWidget(m_statusLabel, 1);
    topRow->addWidget(m_refreshButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(topRow);
    layout->addWidget(m_view, 1);

    connect(m_refreshButton, &QPushButton::clicked, this, &HistoryPage::refresh);
    connect(&m_watcher, &QFutureWatcher<QString>::finished, this, &HistoryPage::onLoaded);

    refresh();
}

void HistoryPage::refresh()
{
    if (!m_backend)
        return;

    m_refreshButton->setEnabled(false);
    m_statusLabel->setText(tr("Loading history..."));

    PackageBackend *backend = m_backend;
    QFuture<QString> future = QtConcurrent::run([backend]() { return backend->recentHistory(); });
    m_watcher.setFuture(future);
}

void HistoryPage::onLoaded()
{
    const QString history = m_watcher.result();
    m_view->setPlainText(history.isEmpty() ? tr("No history available.") : history);
    m_statusLabel->setText(tr("Recent activity"));
    m_refreshButton->setEnabled(true);
}
