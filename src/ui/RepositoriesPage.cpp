#include "RepositoriesPage.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "AddRepositoryDialog.h"
#include "AppIcons.h"
#include "PackageActions.h"

namespace {
enum Column { EnabledColumn = 0, NameColumn, SourceColumn, UrlColumn, ColumnCount };
}

RepositoriesPage::RepositoriesPage(const QVector<RepositorySource> &sources, QWidget *parent)
    : QWidget(parent), m_sources(sources)
{
    m_table = new QTableWidget(this);
    m_table->setColumnCount(ColumnCount);
    m_table->setHorizontalHeaderLabels({tr("Enabled"), tr("Name"), tr("Source"), tr("URL")});
    m_table->horizontalHeader()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(UrlColumn, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_refreshButton = new QPushButton(AppIcons::refresh(), tr("Refresh"), this);
    m_addButton = new QPushButton(AppIcons::addItem(), tr("Add Repository..."), this);
    m_updateListsButton = new QPushButton(AppIcons::update(), tr("Update Package Lists"), this);

    m_statusLabel = new QLabel(tr("Loading repositories..."), this);

    auto *topRow = new QHBoxLayout;
    topRow->addWidget(m_statusLabel, 1);
    topRow->addWidget(m_addButton);
    topRow->addWidget(m_updateListsButton);
    topRow->addWidget(m_refreshButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(topRow);
    layout->addWidget(m_table, 1);

    connect(m_refreshButton, &QPushButton::clicked, this, &RepositoriesPage::refresh);
    connect(m_addButton, &QPushButton::clicked, this, &RepositoriesPage::onAddRepositoryClicked);
    connect(m_updateListsButton, &QPushButton::clicked, this, &RepositoriesPage::onUpdateListsClicked);
    connect(m_table, &QTableWidget::itemChanged, this, &RepositoriesPage::onItemChanged);
    connect(&m_listWatcher, &QFutureWatcher<QVector<RepositoryRow>>::finished, this,
            &RepositoriesPage::onLoaded);
    connect(&m_toggleWatcher, &QFutureWatcher<OperationResult>::finished, this,
            &RepositoriesPage::onToggleFinished);
    connect(&m_addWatcher, &QFutureWatcher<OperationResult>::finished, this,
            &RepositoriesPage::onAddFinished);

    refresh();
}

void RepositoriesPage::refresh()
{
    setBusy(true);
    m_statusLabel->setText(tr("Loading repositories..."));

    const QVector<RepositorySource> sources = m_sources;
    QFuture<QVector<RepositoryRow>> future = QtConcurrent::run([sources]() {
        QVector<RepositoryRow> rows;
        for (const RepositorySource &source : sources) {
            if (!source.backend)
                continue;
            for (const RepositoryInfo &info : source.backend->listRepositories())
                rows.append({source.backend, source.label, info});
        }
        return rows;
    });
    m_listWatcher.setFuture(future);
}

void RepositoriesPage::onLoaded()
{
    m_rows = m_listWatcher.result();

    m_updatingTable = true;
    m_table->setRowCount(m_rows.size());
    for (int i = 0; i < m_rows.size(); ++i) {
        const RepositoryRow &row = m_rows.at(i);

        auto *checkItem = new QTableWidgetItem();
        checkItem->setFlags((checkItem->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        checkItem->setCheckState(row.info.enabled ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(i, EnabledColumn, checkItem);

        auto *nameItem = new QTableWidgetItem(row.info.name);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, NameColumn, nameItem);

        auto *sourceItem = new QTableWidgetItem(row.sourceLabel);
        sourceItem->setFlags(sourceItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, SourceColumn, sourceItem);

        auto *urlItem = new QTableWidgetItem(row.info.url);
        urlItem->setFlags(urlItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, UrlColumn, urlItem);
    }
    m_updatingTable = false;

    m_statusLabel->setText(tr("%1 repositories").arg(m_rows.size()));
    setBusy(false);
}

void RepositoriesPage::onItemChanged(QTableWidgetItem *item)
{
    if (m_updatingTable || item->column() != EnabledColumn)
        return;

    const int row = item->row();
    if (row < 0 || row >= m_rows.size())
        return;

    const RepositoryRow &repoRow = m_rows.at(row);
    const bool wantEnabled = (item->checkState() == Qt::Checked);

    const QString verb = wantEnabled ? tr("Enable") : tr("Disable");
    const auto reply = QMessageBox::question(
        this, verb,
        tr("%1 repository \"%2\"?\n\nThis requires administrator privileges.").arg(verb, repoRow.info.name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply != QMessageBox::Yes) {
        m_updatingTable = true;
        item->setCheckState(repoRow.info.enabled ? Qt::Checked : Qt::Unchecked);
        m_updatingTable = false;
        return;
    }

    applyToggle(row, wantEnabled);
}

void RepositoriesPage::applyToggle(int row, bool enabled)
{
    setBusy(true);
    m_statusLabel->setText(tr("Updating repository..."));

    PackageBackend *backend = m_rows.at(row).backend;
    const QString repoId = m_rows.at(row).info.id;
    QFuture<OperationResult> future = QtConcurrent::run(
        [backend, repoId, enabled]() { return backend->setRepositoryEnabled(repoId, enabled); });
    m_toggleWatcher.setFuture(future);
}

void RepositoriesPage::onToggleFinished()
{
    const OperationResult result = m_toggleWatcher.result();
    if (!result.success) {
        QMessageBox::critical(this, tr("Repository Update Failed"),
                               tr("Failed to update repository.\n\n%1").arg(result.output.trimmed()));
    }
    refresh(); // reload so the table reflects the real on-disk state either way
}

PackageBackend *RepositoriesPage::pickSourceBackend()
{
    if (m_sources.size() == 1)
        return m_sources.first().backend;
    if (m_sources.isEmpty())
        return nullptr;

    QStringList labels;
    for (const RepositorySource &source : m_sources)
        labels << source.label;

    bool ok = false;
    const QString chosen =
        QInputDialog::getItem(this, tr("Add Repository"), tr("Add to:"), labels, 0, false, &ok);
    if (!ok)
        return nullptr;

    for (const RepositorySource &source : m_sources) {
        if (source.label == chosen)
            return source.backend;
    }
    return nullptr;
}

void RepositoriesPage::onAddRepositoryClicked()
{
    PackageBackend *backend = pickSourceBackend();
    if (!backend)
        return;

    AddRepositoryDialog dialog(backend->repositoryAddFields(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const RepositoryAddValues values = dialog.values();

    setBusy(true);
    m_statusLabel->setText(tr("Adding repository..."));

    QFuture<OperationResult> future =
        QtConcurrent::run([backend, values]() { return backend->addRepository(values); });
    m_addWatcher.setFuture(future);
}

void RepositoriesPage::onAddFinished()
{
    const OperationResult result = m_addWatcher.result();
    if (!result.success) {
        QMessageBox::critical(this, tr("Add Repository Failed"),
                               tr("Failed to add repository.\n\n%1").arg(result.output.trimmed()));
    }
    refresh();
}

void RepositoriesPage::onUpdateListsClicked()
{
    if (m_sources.isEmpty())
        return;

    const QVector<RepositorySource> sources = m_sources;
    setBusy(true);
    m_statusLabel->setText(tr("Updating package lists..."));

    PackageActions::confirmAndRun(
        this, tr("Update Package Lists"),
        tr("Refresh repository/package metadata now?\n\nThis may require administrator privileges and can "
           "take a moment."),
        [sources]() -> OperationResult {
            OperationResult combined;
            combined.success = true;
            for (const RepositorySource &source : sources) {
                if (!source.backend)
                    continue;
                const OperationResult result = source.backend->refreshMetadata();
                combined.success = combined.success && result.success;
                if (!result.output.isEmpty())
                    combined.output += result.output + '\n';
            }
            return combined;
        },
        [this](bool) { onUpdateListsFinished(); });
}

void RepositoriesPage::onUpdateListsFinished()
{
    setBusy(false);
    m_statusLabel->setText(tr("%1 repositories").arg(m_rows.size()));
}

void RepositoriesPage::setBusy(bool busy)
{
    m_refreshButton->setEnabled(!busy);
    m_addButton->setEnabled(!busy);
    m_updateListsButton->setEnabled(!busy);
    m_table->setEnabled(!busy);
}
