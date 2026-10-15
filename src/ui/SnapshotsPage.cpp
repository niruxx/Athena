#include "SnapshotsPage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "M3Dialog.h"
#include "M3List.h"
#include "M3Widgets.h"
#include "MaterialTheme.h"
#include "TerminalOutputDialog.h"

namespace {

constexpr int kIndexRole = Qt::UserRole;

QString snapshotSymbol(const SystemTools::Snapshot &snapshot)
{
    if (snapshot.kind == QLatin1String("pre"))
        return QStringLiteral("history");
    if (snapshot.kind == QLatin1String("post"))
        return QStringLiteral("update");
    if (snapshot.kind.contains(QLatin1Char('B')))
        return QStringLiteral("restart_alt");
    return QStringLiteral("backup");
}

// What kind of snapshot it is, in words: Snapper's pre/post pairs and
// Timeshift's schedule tags.
QString snapshotKind(const SystemTools::Snapshot &snapshot)
{
    if (snapshot.kind == QLatin1String("pre"))
        return QObject::tr("before a change");
    if (snapshot.kind == QLatin1String("post"))
        return snapshot.pairedWith.isEmpty() ? QObject::tr("after a change")
                                             : QObject::tr("after the change in #%1").arg(snapshot.pairedWith);
    if (snapshot.kind == QLatin1String("single") || snapshot.kind.isEmpty())
        return QString();
    QStringList tags;
    for (const QChar tag : snapshot.kind) {
        switch (tag.toLatin1()) {
        case 'O': tags << QObject::tr("on demand"); break;
        case 'B': tags << QObject::tr("at boot"); break;
        case 'H': tags << QObject::tr("hourly"); break;
        case 'D': tags << QObject::tr("daily"); break;
        case 'W': tags << QObject::tr("weekly"); break;
        case 'M': tags << QObject::tr("monthly"); break;
        default: break;
        }
    }
    return tags.join(QStringLiteral(", "));
}

} // namespace

SnapshotsPage::SnapshotsPage(QWidget *parent) : QWidget(parent), m_tool(SystemTools::snapshotTool())
{
    QVBoxLayout *intro = nullptr;
    QFrame *introCard = M3::section(
        this, QStringLiteral("history"), tr("Snapshots"),
        m_tool == SystemTools::SnapshotTool::None
            ? tr("Neither Snapper nor Timeshift is set up. A snapshot tool keeps copies of your system you can go back to "
                 "if an update goes wrong. Set up Snapper (on a Btrfs root) or Timeshift, and its snapshots show up here — "
                 "Athena can also take one before every change (Settings → System).")
            : m_tool == SystemTools::SnapshotTool::Snapper
            ? tr("Copies of your system that Snapper keeps, including the ones taken before changes. Restore one to go back "
                 "to how the system was when it was taken.")
            : tr("Copies of your system that Timeshift keeps. Timeshift needs administrator access even to list them."),
        intro);
    auto *actions = new QHBoxLayout;
    m_takeButton = new QPushButton(tr("Take a snapshot"), introCard);
    MaterialTheme::styleButton(m_takeButton, QStringLiteral("filled"), QStringLiteral("add"));
    m_adminButton = new QPushButton(tr("Show snapshots"), introCard);
    MaterialTheme::styleButton(m_adminButton, QStringLiteral("tonal"), QStringLiteral("lock"));
    m_adminButton->setToolTip(tr("Lists them with administrator access (asks for your password)"));
    m_refreshButton = new QPushButton(tr("Refresh"), introCard);
    MaterialTheme::styleButton(m_refreshButton, QStringLiteral("text"), QStringLiteral("refresh"));
    actions->addWidget(m_takeButton);
    actions->addWidget(m_adminButton);
    actions->addWidget(m_refreshButton);
    actions->addStretch(1);
    intro->addLayout(actions);
    const bool available = m_tool != SystemTools::SnapshotTool::None;
    m_takeButton->setVisible(available);
    m_refreshButton->setVisible(available);
    m_adminButton->hide();

    m_status = M3::label(QString(), QStringLiteral("muted"), this);
    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    M3List::apply(m_list);
    m_list->setVisible(available);

    m_deleteButton = new QPushButton(tr("Delete"), this);
    MaterialTheme::styleButton(m_deleteButton, QStringLiteral("text"), QStringLiteral("delete"));
    m_restoreButton = new QPushButton(tr("Restore…"), this);
    MaterialTheme::styleButton(m_restoreButton, QStringLiteral("filled"), QStringLiteral("restore"));
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(m_status, 1);
    bottom->addWidget(m_deleteButton);
    bottom->addWidget(m_restoreButton);
    m_deleteButton->setVisible(available);
    m_restoreButton->setVisible(available);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(14);
    layout->addWidget(introCard);
    layout->addWidget(m_list, 1);
    layout->addLayout(bottom);
    if (!available)
        layout->addStretch(1);

    connect(m_takeButton, &QPushButton::clicked, this, &SnapshotsPage::takeSnapshot);
    connect(m_refreshButton, &QPushButton::clicked, this, [this]() { load(m_loadedAsAdmin); });
    connect(m_adminButton, &QPushButton::clicked, this, [this]() { load(true); });
    connect(m_deleteButton, &QPushButton::clicked, this, &SnapshotsPage::deleteSelected);
    connect(m_restoreButton, &QPushButton::clicked, this, &SnapshotsPage::restoreSelected);
    connect(m_list, &QListWidget::currentRowChanged, this, &SnapshotsPage::updateActions);
    connect(&m_watcher, &QFutureWatcher<Loaded>::finished, this, &SnapshotsPage::onLoaded);

    updateActions();
    refresh();
}

void SnapshotsPage::refresh()
{
    if (m_tool == SystemTools::SnapshotTool::None)
        return;
    if (m_tool == SystemTools::SnapshotTool::Timeshift && !m_loadedAsAdmin) {
        // Don't prompt for a password just because the tab was opened.
        m_adminButton->show();
        m_status->setText(tr("Choose Show snapshots to list them."));
        return;
    }
    load(m_loadedAsAdmin);
}

void SnapshotsPage::load(bool asAdmin)
{
    if (m_watcher.isRunning())
        return;
    m_status->setText(tr("Loading snapshots…"));
    m_refreshButton->setEnabled(false);
    m_adminButton->setEnabled(false);
    m_watcher.setFuture(QtConcurrent::run([asAdmin]() {
        Loaded loaded;
        loaded.asAdmin = asAdmin;
        loaded.snapshots = SystemTools::listSnapshots(0, asAdmin, &loaded.error);
        return loaded;
    }));
}

void SnapshotsPage::onLoaded()
{
    const Loaded loaded = m_watcher.result();
    m_refreshButton->setEnabled(true);
    m_adminButton->setEnabled(true);
    m_snapshots = loaded.snapshots;
    if (loaded.error.isEmpty())
        m_loadedAsAdmin = loaded.asAdmin;

    m_list->clear();
    for (int i = 0; i < m_snapshots.size(); ++i) {
        const SystemTools::Snapshot &snapshot = m_snapshots[i];
        const QString title = !snapshot.description.isEmpty() ? snapshot.description
                              : m_tool == SystemTools::SnapshotTool::Snapper ? tr("Snapshot #%1").arg(snapshot.id)
                                                                              : snapshot.id;
        auto *item = new QListWidgetItem(title, m_list);
        item->setData(kIndexRole, i);
        QStringList parts;
        if (m_tool == SystemTools::SnapshotTool::Snapper)
            parts << tr("#%1").arg(snapshot.id);
        if (const QString kind = snapshotKind(snapshot); !kind.isEmpty())
            parts << kind;
        if (!snapshot.user.isEmpty() && snapshot.user != QLatin1String("root"))
            parts << tr("by %1").arg(snapshot.user);
        item->setData(M3List::SubtitleRole, parts.join(QStringLiteral(" · ")));
        item->setData(M3List::TrailingRole, snapshot.time.isValid() ? QLocale().toString(snapshot.time, QLocale::ShortFormat) : QString());
        item->setData(M3List::SymbolRole, snapshotSymbol(snapshot));
    }

    if (!loaded.error.isEmpty()) {
        // Snapper refuses users its config doesn't allow (ALLOW_USERS);
        // offer listing them as administrator instead.
        m_adminButton->show();
        m_status->setText(loaded.asAdmin ? tr("Couldn't list snapshots: %1").arg(loaded.error)
                                         : tr("Your account can't list %1's snapshots without administrator access.")
                                               .arg(SystemTools::snapshotToolName(m_tool)));
    } else {
        m_adminButton->setVisible(m_tool == SystemTools::SnapshotTool::Timeshift && !m_loadedAsAdmin);
        m_status->setText(m_snapshots.isEmpty() ? tr("No snapshots yet.") : tr("%n snapshot(s)", "", m_snapshots.size()));
    }
    updateActions();
}

const SystemTools::Snapshot *SnapshotsPage::selected() const
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return nullptr;
    const int index = item->data(kIndexRole).toInt();
    return index >= 0 && index < m_snapshots.size() ? &m_snapshots[index] : nullptr;
}

void SnapshotsPage::updateActions()
{
    const bool any = selected() != nullptr;
    m_deleteButton->setEnabled(any);
    m_restoreButton->setEnabled(any);
}

void SnapshotsPage::takeSnapshot()
{
    M3Dialog dialog(this);
    dialog.setHeadline(tr("Take a snapshot"), QStringLiteral("backup"));
    dialog.setSupportingText(tr("Saves the system as it is now with %1, so you can come back to it later.")
                                 .arg(SystemTools::snapshotToolName(m_tool)));
    auto *description = new QLineEdit(&dialog);
    description->setPlaceholderText(tr("What it's for, e.g. Before trying a new kernel"));
    description->setText(tr("Taken with Athena"));
    description->selectAll();
    dialog.body()->addWidget(description);
    connect(dialog.addAction(tr("Cancel"), QStringLiteral("text")), &QPushButton::clicked, &dialog, &QDialog::reject);
    QPushButton *take = dialog.addAction(tr("Take snapshot"), QStringLiteral("filled"), QStringLiteral("backup"));
    take->setDefault(true);
    connect(take, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString text = description->text().trimmed().isEmpty() ? tr("Taken with Athena") : description->text().trimmed();
    TerminalOutputDialog terminal(tr("Take a snapshot"), window());
    terminal.runCommandsModal(SystemTools::snapshotCommands(text), /*autoCloseOnSuccess=*/true);
    load(m_loadedAsAdmin || m_tool == SystemTools::SnapshotTool::Timeshift);
}

void SnapshotsPage::deleteSelected()
{
    const SystemTools::Snapshot *snapshot = selected();
    if (!snapshot)
        return;
    const SystemTools::Snapshot target = *snapshot;
    const QString name = m_list->currentItem()->text();
    if (M3Message::question(this, tr("Delete snapshot"),
                            tr("Delete \"%1\"? You won't be able to restore it afterwards.").arg(name),
                            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
        != QMessageBox::Yes)
        return;
    TerminalOutputDialog terminal(tr("Delete snapshot"), window());
    terminal.runCommandsModal(SystemTools::deleteSnapshotCommands(target), /*autoCloseOnSuccess=*/true);
    load(m_loadedAsAdmin);
}

void SnapshotsPage::restoreSelected()
{
    const SystemTools::Snapshot *snapshot = selected();
    if (!snapshot)
        return;
    const SystemTools::Snapshot target = *snapshot;
    const bool snapper = m_tool == SystemTools::SnapshotTool::Snapper;

    M3Dialog dialog(this);
    dialog.setHeadline(tr("Restore snapshot"), QStringLiteral("restore"));
    // Wide enough that the options' explanations wrap to a few lines.
    dialog.setFixedWidth(560);
    const QString when = target.time.isValid() ? QLocale().toString(target.time, QLocale::ShortFormat) : target.id;
    dialog.setSupportingText(snapper ? tr("Go back to \"%1\", from %2. Choose how:").arg(m_list->currentItem()->text(), when)
                                     : tr("Timeshift copies the files from \"%1\" (%2) back over your system, then restarts the "
                                          "computer. Save your work and close other apps first. The bootloader is left as it is.")
                                           .arg(m_list->currentItem()->text(), when));
    QRadioButton *rollback = nullptr;
    QRadioButton *undo = nullptr;
    if (snapper) {
        auto option = [&dialog](const QString &title, const QString &text) {
            auto *radio = new QRadioButton(title, &dialog);
            dialog.body()->addWidget(radio);
            auto *detail = M3::label(text, QStringLiteral("muted"), &dialog);
            detail->setWordWrap(true);
            detail->setContentsMargins(28, 0, 0, 8);
            // Reserve the wrapped height up front (the dialog sizes itself
            // before the label knows its width).
            detail->setMinimumHeight(detail->heightForWidth(560 - 48 - 28) + 8);
            dialog.body()->addWidget(detail);
            return radio;
        };
        rollback = option(tr("Roll back at the next restart"),
                          tr("Your next boot starts from a writable copy of this snapshot; the system as it is now is kept "
                             "as a snapshot too. Needs a Btrfs root that boots its default subvolume — where /etc/fstab "
                             "names a subvolume (subvol=@), this has no effect."));
        undo = option(tr("Undo the changes since, right now"),
                      tr("Puts every file Snapper tracks back the way it was in this snapshot, without a restart. Apps "
                         "that are running keep using the old files until they're restarted."));
        rollback->setChecked(true);
    }
    connect(dialog.addAction(tr("Cancel"), QStringLiteral("text")), &QPushButton::clicked, &dialog, &QDialog::reject);
    QPushButton *restore = dialog.addAction(tr("Restore"), QStringLiteral("danger"), QStringLiteral("restore"));
    connect(restore, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const SystemTools::RestoreMode mode =
        undo && undo->isChecked() ? SystemTools::RestoreMode::UndoChanges : SystemTools::RestoreMode::Rollback;
    TerminalOutputDialog terminal(tr("Restore snapshot"), window());
    const bool ok = terminal.runCommandsModal(SystemTools::restoreSnapshotCommands(target, mode), /*autoCloseOnSuccess=*/false);
    if (ok && snapper && mode == SystemTools::RestoreMode::Rollback)
        M3Message::information(this, tr("Restart to finish"),
                               tr("The rollback takes effect the next time you start the computer."));
    load(m_loadedAsAdmin);
}
