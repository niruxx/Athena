#include "UpdatesPage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "AppIcons.h"
#include "PackageBrowser.h"

UpdatesPage::UpdatesPage(PackageBackend *backend, QWidget *parent) : QWidget(parent), m_backend(backend)
{
    m_refreshButton = new QPushButton(AppIcons::refresh(), tr("Refresh"), this);
    m_browser = new PackageBrowser(backend, PackageBrowser::Mode::Updates, this);
    m_statusLabel = new QLabel(tr("Checking for updates..."), this);

    auto *topRow = new QHBoxLayout;
    topRow->addWidget(m_statusLabel, 1);
    topRow->addWidget(m_refreshButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(topRow);
    layout->addWidget(m_browser, 1);

    connect(m_refreshButton, &QPushButton::clicked, this, &UpdatesPage::refresh);
    connect(m_browser, &PackageBrowser::refreshRequested, this, &UpdatesPage::refresh);
    connect(&m_watcher, &QFutureWatcher<QVector<PackageInfo>>::finished, this, &UpdatesPage::onLoaded);

    refresh();
}

void UpdatesPage::refresh()
{
    if (!m_backend)
        return;

    m_refreshButton->setEnabled(false);
    m_browser->setBusy(true);
    m_statusLabel->setText(tr("Checking for updates..."));

    PackageBackend *backend = m_backend;
    QFuture<QVector<PackageInfo>> future = QtConcurrent::run([backend]() { return backend->listUpdates(); });
    m_watcher.setFuture(future);
}

void UpdatesPage::onLoaded()
{
    const QVector<PackageInfo> updates = m_watcher.result();
    m_browser->setPackages(updates);
    m_statusLabel->setText(updates.isEmpty() ? tr("Everything is up to date")
                                              : tr("%1 updates available").arg(updates.size()));
    m_refreshButton->setEnabled(true);
    m_browser->setBusy(false);
}
