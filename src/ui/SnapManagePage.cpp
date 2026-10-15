#include "SnapManagePage.h"

#include <algorithm>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "AppCatalog.h"
#include "AthenaStyle.h"
#include "M3Dialog.h"
#include "M3List.h"
#include "M3Widgets.h"
#include "MaterialTheme.h"
#include "TerminalOutputDialog.h"

namespace {

constexpr int kNameRole = Qt::UserRole;

// A symbol for an interface's row, by what it reaches.
QString interfaceSymbol(const QString &interface)
{
    if (interface.startsWith(QLatin1String("network")))
        return QStringLiteral("wifi");
    if (interface.startsWith(QLatin1String("audio")) || interface == QLatin1String("pulseaudio"))
        return QStringLiteral("volume_up");
    if (interface == QLatin1String("home") || interface == QLatin1String("removable-media")
        || interface == QLatin1String("personal-files") || interface == QLatin1String("system-files"))
        return QStringLiteral("folder");
    if (interface == QLatin1String("camera"))
        return QStringLiteral("image");
    if (interface.startsWith(QLatin1String("bluetooth")) || interface == QLatin1String("bluez"))
        return QStringLiteral("bluetooth");
    if (interface == QLatin1String("x11") || interface == QLatin1String("wayland") || interface == QLatin1String("desktop")
        || interface.startsWith(QLatin1String("desktop-")) || interface == QLatin1String("unity7"))
        return QStringLiteral("desktop_windows");
    if (interface == QLatin1String("opengl") || interface == QLatin1String("hardware-observe"))
        return QStringLiteral("memory");
    if (interface.startsWith(QLatin1String("ssh")) || interface.startsWith(QLatin1String("password"))
        || interface == QLatin1String("gpg-keys"))
        return QStringLiteral("key");
    if (interface.startsWith(QLatin1String("raw-usb")) || interface == QLatin1String("joystick")
        || interface == QLatin1String("u2f-devices"))
        return QStringLiteral("usb");
    if (interface == QLatin1String("content"))
        return QStringLiteral("extension");
    if (interface.contains(QLatin1String("dbus")) || interface.endsWith(QLatin1String("-service"))
        || interface.endsWith(QLatin1String("-support")))
        return QStringLiteral("hub");
    return QStringLiteral("cable");
}

} // namespace

SnapManagePage::SnapManagePage(QWidget *parent) : QWidget(parent)
{
    // Installed snaps, on the left.
    m_list = new QListWidget(this);
    M3List::apply(m_list, {-1, -1, /*plain=*/true});
    m_list->setMinimumWidth(260);
    m_listStatus = M3::label(tr("Loading snaps…"), QStringLiteral("muted"), this);
    m_refreshButton = new QPushButton(tr("Refresh"), this);
    MaterialTheme::styleButton(m_refreshButton, QStringLiteral("text"), QStringLiteral("refresh"));
    auto *leftTop = new QHBoxLayout;
    leftTop->addWidget(m_listStatus, 1);
    leftTop->addWidget(m_refreshButton);
    auto *left = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, AthenaStyle::kSidebarGap, 0);
    leftLayout->addLayout(leftTop);
    leftLayout->addWidget(m_list, 1);

    // Header card.
    auto *header = M3::card(QStringLiteral("card"), this);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 16, 20, 16);
    headerLayout->setSpacing(16);
    m_icon = new QLabel(header);
    m_icon->setFixedSize(48, 48);
    headerLayout->addWidget(m_icon, 0, Qt::AlignTop);
    auto *headerText = new QVBoxLayout;
    headerText->setSpacing(2);
    m_title = M3::label(QString(), QStringLiteral("title"), header);
    m_summary = M3::label(QString(), QString(), header);
    m_summary->setWordWrap(true);
    m_meta = M3::label(QString(), QStringLiteral("muted"), header);
    m_meta->setWordWrap(true);
    headerText->addWidget(m_title);
    headerText->addWidget(m_summary);
    headerText->addWidget(m_meta);
    headerLayout->addLayout(headerText, 1);

    // Channel.
    QVBoxLayout *channelLayout = nullptr;
    QFrame *channelCard = M3::section(this, QStringLiteral("alt_route"), tr("Channel"),
                                      tr("A snap follows a channel: a track (such as latest) and a risk level, from stable "
                                         "through candidate and beta to edge. Switching refreshes it to that channel's release "
                                         "now, and it keeps following that channel."),
                                      channelLayout);
    m_tracking = M3::label(QString(), QStringLiteral("label"), channelCard);
    channelLayout->addWidget(m_tracking);
    auto *channelRow = new QHBoxLayout;
    m_channelCombo = new QComboBox(channelCard);
    m_channelCombo->setMinimumWidth(240);
    m_switchChannelButton = new QPushButton(tr("Switch channel"), channelCard);
    MaterialTheme::styleButton(m_switchChannelButton, QStringLiteral("filled"), QStringLiteral("alt_route"));
    channelRow->addWidget(m_channelCombo);
    channelRow->addWidget(m_switchChannelButton);
    channelRow->addStretch(1);
    channelLayout->addLayout(channelRow);

    // State: enabled, and reverting.
    QVBoxLayout *stateLayout = nullptr;
    QFrame *stateCard = M3::section(this, QStringLiteral("power_settings_new"), tr("State"), QString(), stateLayout);
    stateLayout->setSpacing(0);
    stateLayout->addWidget(M3::switchRow(stateCard, QStringLiteral("power_settings_new"), tr("Enabled"),
                                         tr("A disabled snap stays installed with its data, but it can't run and its "
                                            "services stop."),
                                         m_enabledSwitch));
    stateLayout->addWidget(M3::divider(stateCard));
    auto *revertRow = new QHBoxLayout;
    revertRow->setContentsMargins(0, 8, 0, 8);
    revertRow->setSpacing(16);
    revertRow->addWidget(M3::symbolLabel(QStringLiteral("undo"), 22, stateCard, true));
    auto *revertText = new QVBoxLayout;
    revertText->setSpacing(2);
    revertText->addWidget(M3::label(tr("Previous revision"), QString(), stateCard));
    m_revertText = M3::label(QString(), QStringLiteral("muted"), stateCard);
    m_revertText->setWordWrap(true);
    revertText->addWidget(m_revertText);
    revertRow->addLayout(revertText, 1);
    m_revertButton = new QPushButton(tr("Revert"), stateCard);
    MaterialTheme::styleButton(m_revertButton, QStringLiteral("tonal"), QStringLiteral("undo"));
    revertRow->addWidget(m_revertButton, 0, Qt::AlignVCenter);
    stateLayout->addLayout(revertRow);

    // Interfaces.
    QVBoxLayout *connectionsLayout = nullptr;
    QFrame *connectionsCard = M3::section(
        this, QStringLiteral("cable"), tr("Permissions"),
        tr("Interfaces let a confined snap reach things outside its sandbox. Some connect automatically when it's "
           "installed; you can connect or disconnect any of them here."),
        connectionsLayout);
    m_connectionsNote = M3::label(QString(), QStringLiteral("label"), connectionsCard);
    m_connectionsNote->setWordWrap(true);
    connectionsLayout->addWidget(m_connectionsNote);
    m_connectionRows = new QVBoxLayout;
    m_connectionRows->setSpacing(0);
    connectionsLayout->addLayout(m_connectionRows);

    m_detail = new QWidget(this);
    auto *detailLayout = new QVBoxLayout(m_detail);
    detailLayout->setContentsMargins(0, 0, 8, 8);
    detailLayout->setSpacing(16);
    detailLayout->addWidget(header);
    detailLayout->addWidget(channelCard);
    detailLayout->addWidget(stateCard);
    detailLayout->addWidget(connectionsCard);
    detailLayout->addStretch(1);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setProperty("m3", QStringLiteral("page"));
    scroll->setWidget(m_detail);

    auto *empty = M3::label(tr("Select a snap on the left to manage it."), QStringLiteral("muted"), this);
    empty->setAlignment(Qt::AlignCenter);
    m_stack = new QStackedWidget(this);
    m_stack->addWidget(empty);
    m_stack->addWidget(scroll);

    auto *splitter = new QSplitter(this);
    AthenaStyle::makeSidebarSplitter(splitter);
    splitter->addWidget(left);
    splitter->addWidget(m_stack);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({300, 900});
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(splitter);

    connect(m_refreshButton, &QPushButton::clicked, this, &SnapManagePage::refresh);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { loadCurrent(); });
    connect(&m_listWatcher, &QFutureWatcher<QVector<SnapTools::Snap>>::finished, this, &SnapManagePage::onListLoaded);
    connect(&m_detailsWatcher, &QFutureWatcher<Loaded>::finished, this, &SnapManagePage::onDetailsLoaded);

    connect(m_channelCombo, &QComboBox::currentTextChanged, this, [this](const QString &channel) {
        m_switchChannelButton->setEnabled(m_channelCombo->isEnabled() && !channel.isEmpty() && channel != m_trackingChannel);
    });
    connect(m_switchChannelButton, &QPushButton::clicked, this, [this]() {
        const QString name = currentName();
        const QString channel = m_channelCombo->currentText();
        if (name.isEmpty() || channel.isEmpty())
            return;
        runInTerminal(tr("Switch %1 to %2").arg(name, channel), SnapTools::switchChannelCommands(name, channel));
    });
    connect(m_revertButton, &QPushButton::clicked, this, [this]() {
        const QString name = currentName();
        if (name.isEmpty())
            return;
        if (M3Message::question(this, tr("Revert %1").arg(name),
                                tr("Go back to the revision of %1 installed before its last refresh? Its data from that "
                                   "revision is restored too.")
                                    .arg(name),
                                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
            != QMessageBox::Yes)
            return;
        runInTerminal(tr("Revert %1").arg(name), SnapTools::revertCommands(name));
    });
    connect(m_enabledSwitch, &QAbstractButton::toggled, this, [this](bool on) {
        if (m_updating || currentName().isEmpty())
            return;
        const QString name = currentName();
        runInTerminal(on ? tr("Enable %1").arg(name) : tr("Disable %1").arg(name), SnapTools::setEnabledCommands(name, on));
    });
    connect(&m_connectWatcher, &QFutureWatcher<ProcessRunner::Result>::finished, this, [this]() {
        const ProcessRunner::Result result = m_connectWatcher.result();
        if (!result.started || result.exitCode != 0)
            M3Message::critical(this, tr("Couldn't change the connection"),
                                (result.stdErr + result.stdOut).trimmed().isEmpty() ? tr("snap didn't say why.")
                                                                                     : (result.stdErr + result.stdOut).trimmed());
        loadCurrent();
    });

    refresh();
}

QString SnapManagePage::currentName() const
{
    QListWidgetItem *item = m_list->currentItem();
    return item ? item->data(kNameRole).toString() : QString();
}

const SnapTools::Snap *SnapManagePage::currentSnap() const
{
    const QString name = currentName();
    for (const SnapTools::Snap &snap : m_snaps) {
        if (snap.name == name)
            return &snap;
    }
    return nullptr;
}

void SnapManagePage::refresh()
{
    if (m_listWatcher.isRunning())
        return;
    m_refreshButton->setEnabled(false);
    m_listStatus->setText(tr("Loading snaps…"));
    m_listWatcher.setFuture(QtConcurrent::run([]() { return SnapTools::installed(); }));
}

void SnapManagePage::onListLoaded()
{
    const QString previous = currentName();
    m_snaps = m_listWatcher.result();
    std::sort(m_snaps.begin(), m_snaps.end(),
              [](const SnapTools::Snap &a, const SnapTools::Snap &b) { return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0; });

    const QSignalBlocker blocker(m_list);
    m_list->clear();
    for (const SnapTools::Snap &snap : m_snaps) {
        auto *item = new QListWidgetItem(snap.name, m_list);
        item->setData(kNameRole, snap.name);
        item->setData(M3List::SubtitleRole,
                      tr("%1 · %2").arg(snap.version, snap.tracking == QLatin1String("-") ? tr("local") : snap.tracking));
        if (snap.disabled())
            item->setData(M3List::TrailingRole, tr("Disabled"));
        if (const AppCatalog::Entry *entry = AppCatalog::find(snap.name); entry && !AppCatalog::iconFor(*entry).isNull())
            item->setIcon(AppCatalog::iconFor(*entry));
        else
            item->setData(M3List::SymbolRole, snap.notes.contains(QStringLiteral("base")) || snap.notes.contains(QStringLiteral("snapd"))
                                                  ? QStringLiteral("deployed_code")
                                                  : QStringLiteral("widgets"));
        if (snap.name == previous)
            m_list->setCurrentItem(item);
    }
    m_listStatus->setText(tr("%n snap(s)", "", m_snaps.size()));
    m_refreshButton->setEnabled(true);
    loadCurrent();
}

void SnapManagePage::loadCurrent()
{
    const SnapTools::Snap *snap = currentSnap();
    if (!snap) {
        m_stack->setCurrentIndex(0);
        return;
    }
    m_stack->setCurrentIndex(1);
    m_title->setText(snap->name);
    m_meta->setText(tr("%1 · version %2 · revision %3").arg(snap->publisher, snap->version, snap->revision));
    const QIcon icon = m_list->currentItem()->icon();
    m_icon->setPixmap((icon.isNull() ? MaterialTheme::icon(QStringLiteral("widgets"), MaterialTheme::current().primary, 40) : icon)
                          .pixmap(48, 48));
    m_updating = true;
    m_enabledSwitch->setCheckedQuietly(!snap->disabled());
    m_updating = false;
    m_detail->setEnabled(false); // until the details are in

    const QString name = snap->name;
    m_detailsWatcher.setFuture(QtConcurrent::run([name]() {
        Loaded loaded;
        loaded.details = SnapTools::details(name);
        loaded.connections = SnapTools::connections(name);
        return loaded;
    }));
}

void SnapManagePage::onDetailsLoaded()
{
    const Loaded loaded = m_detailsWatcher.result();
    const SnapTools::Snap *snap = currentSnap();
    if (!snap)
        return;
    const SnapTools::Details &details = loaded.details;
    m_summary->setText(details.summary);
    m_summary->setVisible(!details.summary.isEmpty());

    const QString tracking = details.tracking.isEmpty() ? snap->tracking : details.tracking;
    const bool local = tracking.isEmpty() || tracking == QLatin1String("-");
    m_tracking->setText(local ? tr("Installed from a file, not the store — there's no channel to follow.")
                              : tr("Following %1").arg(tracking));
    m_channelCombo->clear();
    m_channelCombo->addItems(details.channels);
    m_channelCombo->setCurrentText(tracking);
    m_channelCombo->setEnabled(!local && details.channels.size() > 1);
    m_trackingChannel = tracking;
    m_switchChannelButton->setEnabled(false); // until another channel is picked

    m_revertButton->setEnabled(details.hasPreviousRevision);
    m_revertText->setText(details.hasPreviousRevision ? tr("Go back to the revision installed before the last refresh.")
                                                      : tr("No earlier revision is kept for this snap."));

    rebuildConnections(loaded.connections, details.confinement == QLatin1String("classic")
                                               || snap->notes.contains(QStringLiteral("classic")));
    m_detail->setEnabled(true);
}

void SnapManagePage::rebuildConnections(const QVector<SnapTools::Connection> &connections, bool classic)
{
    while (QLayoutItem *item = m_connectionRows->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    if (classic) {
        m_connectionsNote->setText(tr("This snap uses classic confinement: it runs without a sandbox, like a regular "
                                      "program, so interfaces don't limit it."));
        m_connectionsNote->show();
        return;
    }
    m_connectionsNote->setText(tr("This snap doesn't ask for any interfaces."));
    m_connectionsNote->setVisible(connections.isEmpty());

    QWidget *card = m_connectionsNote->parentWidget();
    bool first = true;
    for (const SnapTools::Connection &connection : connections) {
        if (!first)
            m_connectionRows->addWidget(M3::divider(card));
        first = false;
        QString supporting = connection.connected() ? tr("Connected to %1").arg(connection.slot) : tr("Not connected");
        if (connection.manual)
            supporting += tr(" · changed by hand");
        M3Switch *toggle = nullptr;
        m_connectionRows->addWidget(M3::switchRow(card, interfaceSymbol(connection.interface), connection.interface,
                                                  supporting, toggle));
        toggle->setCheckedQuietly(connection.connected());
        const QString plug = connection.plug;
        connect(toggle, &QAbstractButton::toggled, this, [this, plug](bool on) {
            if (m_connectWatcher.isRunning())
                return;
            m_detail->setEnabled(false);
            const auto commands = on ? SnapTools::connectCommands(plug) : SnapTools::disconnectCommands(plug);
            m_connectWatcher.setFuture(QtConcurrent::run([commands]() { return ProcessRunner::runSequence(commands, 120000); }));
        });
    }
}

void SnapManagePage::runInTerminal(const QString &title, const QVector<ProcessRunner::Command> &commands)
{
    TerminalOutputDialog terminal(title, window());
    terminal.runCommandsModal(commands, /*autoCloseOnSuccess=*/true);
    refresh(); // the list and the selected snap, either way
}
