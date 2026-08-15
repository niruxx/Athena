#include "InstalledPage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "AppIcons.h"
#include "PackageActions.h"
#include "PackageBrowser.h"

InstalledPage::InstalledPage(PackageBackend *backend, QWidget *parent)
    : QWidget(parent), m_backend(backend)
{
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Filter installed packages..."));

    m_refreshButton = new QPushButton(AppIcons::refresh(), tr("Refresh"), this);
    m_cleanButton = new QPushButton(AppIcons::clean(), tr("Clean Left Behind Dependencies"), this);

    m_browser = new PackageBrowser(backend, PackageBrowser::Mode::InstallRemove, this);

    m_statusLabel = new QLabel(tr("Loading installed packages..."), this);

    auto *topRow = new QHBoxLayout;
    topRow->addWidget(m_filterEdit, 1);
    topRow->addWidget(m_cleanButton);
    topRow->addWidget(m_refreshButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(topRow);
    layout->addWidget(m_browser, 1);
    layout->addWidget(m_statusLabel);

    connect(m_filterEdit, &QLineEdit::textChanged, m_browser, &PackageBrowser::setFilterText);
    connect(m_refreshButton, &QPushButton::clicked, this, &InstalledPage::refresh);
    connect(m_cleanButton, &QPushButton::clicked, this, &InstalledPage::onCleanClicked);
    connect(m_browser, &PackageBrowser::refreshRequested, this, &InstalledPage::refresh);
    connect(&m_watcher, &QFutureWatcher<QVector<PackageInfo>>::finished, this, &InstalledPage::onLoaded);

    refresh();
}

void InstalledPage::refresh()
{
    if (!m_backend)
        return;

    setBusy(true);
    m_statusLabel->setText(tr("Loading installed packages..."));

    PackageBackend *backend = m_backend;
    QFuture<QVector<PackageInfo>> future =
        QtConcurrent::run([backend]() { return backend->listInstalled(); });
    m_watcher.setFuture(future);
}

void InstalledPage::onLoaded()
{
    const QVector<PackageInfo> packages = m_watcher.result();
    m_browser->setPackages(packages);
    m_statusLabel->setText(tr("%1 installed packages").arg(packages.size()));
    setBusy(false);
}

void InstalledPage::onCleanClicked()
{
    if (!m_backend)
        return;

    setBusy(true);
    m_statusLabel->setText(tr("Cleaning up unused dependencies..."));

    PackageActions::cleanUnusedDependencies(this, m_backend, [this](bool success) {
        if (success)
            refresh();
        else
            setBusy(false);
    });
}

void InstalledPage::setBusy(bool busy)
{
    m_refreshButton->setEnabled(!busy);
    m_cleanButton->setEnabled(!busy);
    m_filterEdit->setEnabled(!busy);
    m_browser->setBusy(busy);
}
