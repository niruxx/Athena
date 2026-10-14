#include "RepositoriesPage.h"

#include <QHBoxLayout>
#include <QFrame>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "AddRepositoryDialog.h"
#include "AppIcons.h"
#include "M3Dialog.h"
#include "M3Widgets.h"
#include "MaterialTheme.h"
#include "PackageActions.h"


RepositoriesPage::RepositoriesPage(const QVector<RepositorySource> &sources, QWidget *parent)
    : QWidget(parent), m_sources(sources)
{
    auto *card = new QFrame(this);
    card->setProperty("m3", QStringLiteral("card"));
    m_rowsLayout = new QVBoxLayout(card);
    m_rowsLayout->setContentsMargins(20, 8, 20, 8);
    m_rowsLayout->setSpacing(0);
    auto *scroll = new QScrollArea(this);
    scroll->setProperty("m3", QStringLiteral("page"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    auto *scrollContent = new QWidget(scroll);
    auto *scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(0, 0, 0, 0);
    scrollLayout->addWidget(card);
    scrollLayout->addStretch(1);
    scroll->setWidget(scrollContent);

    m_refreshButton = new QPushButton(AppIcons::refresh(), tr("Refresh"), this);
    MaterialTheme::styleButton(m_refreshButton, QStringLiteral("tonal"), QStringLiteral("refresh"));
    m_addButton = new QPushButton(AppIcons::addItem(), tr("Add Repository..."), this);
    MaterialTheme::styleButton(m_addButton, QStringLiteral("filled"), QStringLiteral("add"));
    m_updateListsButton = new QPushButton(AppIcons::update(), tr("Update Package Lists"), this);
    MaterialTheme::styleButton(m_updateListsButton, QStringLiteral("tonal"), QStringLiteral("sync"));

    m_statusLabel = new QLabel(tr("Loading repositories..."), this);
    m_statusLabel->setProperty("m3", QStringLiteral("muted"));

    auto *topRow = new QHBoxLayout;
    topRow->addWidget(m_statusLabel, 1);
    topRow->addWidget(m_addButton);
    topRow->addWidget(m_updateListsButton);
    topRow->addWidget(m_refreshButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(topRow);
    layout->addWidget(scroll, 1);

    connect(m_refreshButton, &QPushButton::clicked, this, &RepositoriesPage::refresh);
    connect(m_addButton, &QPushButton::clicked, this, &RepositoriesPage::onAddRepositoryClicked);
    connect(m_updateListsButton, &QPushButton::clicked, this, &RepositoriesPage::onUpdateListsClicked);
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

    while (QLayoutItem *item = m_rowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    m_switches.clear();

    const QColor divider = MaterialTheme::current().outlineVariant;
    for (int i = 0; i < m_rows.size(); ++i) {
        const RepositoryRow &repo = m_rows.at(i);
        if (i > 0) {
            auto *line = new QFrame(this);
            line->setFixedHeight(1);
            line->setStyleSheet(QStringLiteral("background: %1;").arg(divider.name()));
            m_rowsLayout->addWidget(line);
        }
        auto *row = new QWidget(this);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 12, 0, 12);
        rowLayout->setSpacing(16);
        auto *text = new QVBoxLayout;
        text->setSpacing(2);
        auto *name = new QLabel(repo.info.name, row);
        QFont bold = name->font();
        bold.setWeight(QFont::DemiBold);
        name->setFont(bold);
        auto *detail = new QLabel(repo.info.url.isEmpty() ? repo.sourceLabel : QStringLiteral("%1 · %2").arg(repo.sourceLabel, repo.info.url), row);
        detail->setProperty("m3", QStringLiteral("muted"));
        detail->setWordWrap(true);
        detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
        text->addWidget(name);
        text->addWidget(detail);
        auto *toggle = new M3Switch(row);
        toggle->setCheckedQuietly(repo.info.enabled);
        toggle->setAccessibleName(tr("Enable %1").arg(repo.info.name));
        connect(toggle, &M3Switch::toggled, this, [this, i](bool on) { onSwitchToggled(i, on); });
        rowLayout->addLayout(text, 1);
        rowLayout->addWidget(toggle, 0, Qt::AlignVCenter);
        m_rowsLayout->addWidget(row);
        m_switches.append(toggle);
    }

    m_statusLabel->setText(tr("%n repositor(ies)", "", m_rows.size()));
    setBusy(false);
}

void RepositoriesPage::onSwitchToggled(int row, bool wantEnabled)
{
    if (row < 0 || row >= m_rows.size())
        return;
    const RepositoryRow &repoRow = m_rows.at(row);
    const QString verb = wantEnabled ? tr("Enable") : tr("Disable");
    const auto reply = M3Message::question(
        this, tr("%1 %2?").arg(verb, repoRow.info.name),
        tr("This changes your system's package sources and needs administrator privileges."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        m_switches[row]->setCheckedQuietly(repoRow.info.enabled);
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
        M3Message::critical(this, tr("Repository Update Failed"),
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
        M3Message::critical(this, tr("Add Repository Failed"),
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
    for (M3Switch *toggle : m_switches)
        toggle->setEnabled(!busy);
}
