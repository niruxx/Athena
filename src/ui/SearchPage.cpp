#include "SearchPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "../models/PackageTableModel.h"
#include "AppIcons.h"
#include "PackageActions.h"
#include "PackageBrowser.h"

SearchPage::SearchPage(PackageBackend *backend, QWidget *parent) : QWidget(parent), m_backend(backend)
{
    m_queryEdit = new QLineEdit(this);
    m_queryEdit->setPlaceholderText(tr("Search for a package..."));
    m_searchButton = new QPushButton(AppIcons::search(), tr("Search"), this);

    auto *topRow = new QHBoxLayout;
    topRow->addWidget(m_queryEdit, 1);
    topRow->addWidget(m_searchButton);

    auto *advancedGroup = new QGroupBox(tr("Advanced Search"), this);
    auto *advancedLayout = new QHBoxLayout(advancedGroup);

    m_dependencyQueryCheck = new QCheckBox(tr("Dependency Query"), this);
    m_dependencyQueryCheck->setToolTip(
        tr("Search by capability instead of name/keyword: find packages that provide or require it."));
    m_dependencyDirectionCombo = new QComboBox(this);
    m_dependencyDirectionCombo->addItem(tr("Provides"));
    m_dependencyDirectionCombo->addItem(tr("Requires"));
    m_dependencyDirectionCombo->setEnabled(false);

    m_repositoryFilterCombo = new QComboBox(this);
    m_repositoryFilterCombo->addItem(tr("All Repositories"));
    m_architectureFilterCombo = new QComboBox(this);
    m_architectureFilterCombo->addItem(tr("All Architectures"));

    advancedLayout->addWidget(m_dependencyQueryCheck);
    advancedLayout->addWidget(m_dependencyDirectionCombo);
    advancedLayout->addSpacing(12);
    advancedLayout->addWidget(new QLabel(tr("Repository:"), this));
    advancedLayout->addWidget(m_repositoryFilterCombo, 1);
    advancedLayout->addSpacing(12);
    advancedLayout->addWidget(new QLabel(tr("Architecture:"), this));
    advancedLayout->addWidget(m_architectureFilterCombo, 1);

    m_browser = new PackageBrowser(backend, PackageBrowser::Mode::InstallRemove, this);

    m_statusLabel = new QLabel(tr("Enter a search term."), this);

    m_downloadButton = new QPushButton(AppIcons::download(), tr("Download Selected"), this);
    m_downloadButton->setToolTip(tr("Download a copy of the checked packages without installing them."));
    m_downloadButton->setEnabled(false);
    m_downloadWithDepsButton = new QPushButton(AppIcons::download(), tr("Download Selected + Dependencies"), this);
    m_downloadWithDepsButton->setToolTip(
        tr("Download the checked packages plus any not-yet-installed packages they require."));
    m_downloadWithDepsButton->setEnabled(false);

    auto *downloadRow = new QHBoxLayout;
    downloadRow->addStretch(1);
    downloadRow->addWidget(m_downloadButton);
    downloadRow->addWidget(m_downloadWithDepsButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(topRow);
    layout->addWidget(advancedGroup);
    layout->addWidget(m_browser, 1);
    layout->addWidget(m_statusLabel);
    layout->addLayout(downloadRow);

    connect(m_queryEdit, &QLineEdit::returnPressed, this, &SearchPage::runSearch);
    connect(m_searchButton, &QPushButton::clicked, this, &SearchPage::runSearch);
    connect(m_browser, &PackageBrowser::refreshRequested, this, &SearchPage::runSearch);
    connect(&m_watcher, &QFutureWatcher<QVector<PackageInfo>>::finished, this, &SearchPage::onSearchFinished);
    connect(m_dependencyQueryCheck, &QCheckBox::toggled, this, &SearchPage::onDependencyQueryToggled);
    connect(m_repositoryFilterCombo, &QComboBox::currentIndexChanged, this, &SearchPage::applyResultFilters);
    connect(m_architectureFilterCombo, &QComboBox::currentIndexChanged, this, &SearchPage::applyResultFilters);
    connect(m_downloadButton, &QPushButton::clicked, this, &SearchPage::downloadSelected);
    connect(m_downloadWithDepsButton, &QPushButton::clicked, this, &SearchPage::downloadSelectedWithDependencies);
    connect(m_browser->model(), &PackageTableModel::checkedChanged, this, &SearchPage::updateDownloadButtonsEnabled);
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
    const bool dependencyMode = m_dependencyQueryCheck->isChecked();
    const bool findRequires = m_dependencyDirectionCombo->currentIndex() == 1;

    QFuture<QVector<PackageInfo>> future = QtConcurrent::run([backend, query, dependencyMode, findRequires]() {
        if (dependencyMode)
            return backend->dependencyQuery(query, findRequires);

        QStringList names;
        for (const PackageInfo &pkg : backend->search(query))
            names.append(pkg.name);
        return backend->packageDetails(names);
    });
    m_watcher.setFuture(future);
}

void SearchPage::onSearchFinished()
{
    m_allResults = m_watcher.result();
    populateFilterCombos();
    applyResultFilters();
    m_statusLabel->setText(tr("%1 results").arg(m_allResults.size()));
    m_searchButton->setEnabled(true);
    m_browser->setBusy(false);
}

void SearchPage::onDependencyQueryToggled(bool checked)
{
    m_dependencyDirectionCombo->setEnabled(checked);
    m_queryEdit->setPlaceholderText(checked ? tr("Enter a package name or capability (e.g. a library soname)...")
                                             : tr("Search for a package..."));
}

void SearchPage::populateFilterCombos()
{
    const QString previousRepo =
        m_repositoryFilterCombo->currentIndex() > 0 ? m_repositoryFilterCombo->currentText() : QString();
    const QString previousArch =
        m_architectureFilterCombo->currentIndex() > 0 ? m_architectureFilterCombo->currentText() : QString();

    QSet<QString> repos;
    QSet<QString> archs;
    for (const PackageInfo &pkg : m_allResults) {
        if (!pkg.repository.isEmpty())
            repos.insert(pkg.repository);
        if (!pkg.architecture.isEmpty())
            archs.insert(pkg.architecture);
    }

    QStringList repoList = repos.values();
    repoList.sort(Qt::CaseInsensitive);
    QStringList archList = archs.values();
    archList.sort(Qt::CaseInsensitive);

    {
        const QSignalBlocker blocker(m_repositoryFilterCombo);
        m_repositoryFilterCombo->clear();
        m_repositoryFilterCombo->addItem(tr("All Repositories"));
        m_repositoryFilterCombo->addItems(repoList);
        const int index = previousRepo.isEmpty() ? 0 : m_repositoryFilterCombo->findText(previousRepo);
        m_repositoryFilterCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
    {
        const QSignalBlocker blocker(m_architectureFilterCombo);
        m_architectureFilterCombo->clear();
        m_architectureFilterCombo->addItem(tr("All Architectures"));
        m_architectureFilterCombo->addItems(archList);
        const int index = previousArch.isEmpty() ? 0 : m_architectureFilterCombo->findText(previousArch);
        m_architectureFilterCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
}

void SearchPage::applyResultFilters()
{
    const QString repoFilter =
        m_repositoryFilterCombo->currentIndex() > 0 ? m_repositoryFilterCombo->currentText() : QString();
    const QString archFilter =
        m_architectureFilterCombo->currentIndex() > 0 ? m_architectureFilterCombo->currentText() : QString();

    QVector<PackageInfo> filtered;
    if (repoFilter.isEmpty() && archFilter.isEmpty()) {
        filtered = m_allResults;
    } else {
        for (const PackageInfo &pkg : m_allResults) {
            if (!repoFilter.isEmpty() && pkg.repository != repoFilter)
                continue;
            if (!archFilter.isEmpty() && pkg.architecture != archFilter)
                continue;
            filtered.append(pkg);
        }
    }
    m_browser->setPackages(filtered);
    updateDownloadButtonsEnabled();
}

void SearchPage::updateDownloadButtonsEnabled()
{
    const bool hasChecked = !m_browser->model()->checkedNames().isEmpty();
    m_downloadButton->setEnabled(hasChecked);
    m_downloadWithDepsButton->setEnabled(hasChecked);
}

void SearchPage::downloadSelected()
{
    runDownload(false);
}

void SearchPage::downloadSelectedWithDependencies()
{
    runDownload(true);
}

void SearchPage::runDownload(bool includeDependencies)
{
    if (!m_backend)
        return;

    const QStringList names = m_browser->model()->checkedNames();
    if (names.isEmpty())
        return;

    const QString destinationDir = QFileDialog::getExistingDirectory(this, tr("Choose Download Folder"));
    if (destinationDir.isEmpty())
        return;

    PackageActions::downloadPackages(this, m_backend, names, destinationDir, includeDependencies, [](bool) {});
}
