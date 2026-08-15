#include "SearchPage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "AppIcons.h"
#include "PackageBrowser.h"

SearchPage::SearchPage(PackageBackend *backend, QWidget *parent) : QWidget(parent), m_backend(backend)
{
    m_queryEdit = new QLineEdit(this);
    m_queryEdit->setPlaceholderText(tr("Search for a package..."));
    m_searchButton = new QPushButton(AppIcons::search(), tr("Search"), this);

    m_browser = new PackageBrowser(backend, PackageBrowser::Mode::InstallRemove, this);

    m_statusLabel = new QLabel(tr("Enter a search term."), this);

    auto *topRow = new QHBoxLayout;
    topRow->addWidget(m_queryEdit, 1);
    topRow->addWidget(m_searchButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(topRow);
    layout->addWidget(m_browser, 1);
    layout->addWidget(m_statusLabel);

    connect(m_queryEdit, &QLineEdit::returnPressed, this, &SearchPage::runSearch);
    connect(m_searchButton, &QPushButton::clicked, this, &SearchPage::runSearch);
    connect(m_browser, &PackageBrowser::refreshRequested, this, &SearchPage::runSearch);
    connect(&m_watcher, &QFutureWatcher<QVector<PackageInfo>>::finished, this, &SearchPage::onSearchFinished);
}

void SearchPage::runSearch()
{
    const QString query = m_queryEdit->text().trimmed();
    if (query.isEmpty() || !m_backend)
        return;

    m_searchButton->setEnabled(false);
    m_browser->setBusy(true);
    m_statusLabel->setText(tr("Searching for \"%1\"...").arg(query));

    PackageBackend *backend = m_backend;
    QFuture<QVector<PackageInfo>> future = QtConcurrent::run([backend, query]() {
        QStringList names;
        for (const PackageInfo &pkg : backend->search(query))
            names.append(pkg.name);
        return backend->packageDetails(names);
    });
    m_watcher.setFuture(future);
}

void SearchPage::onSearchFinished()
{
    const QVector<PackageInfo> results = m_watcher.result();
    m_browser->setPackages(results);
    m_statusLabel->setText(tr("%1 results").arg(results.size()));
    m_searchButton->setEnabled(true);
    m_browser->setBusy(false);
}
