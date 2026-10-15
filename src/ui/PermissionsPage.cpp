#include "PermissionsPage.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPair>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "AppCatalog.h"
#include "AppIcons.h"
#include "M3List.h"
#include "M3Widgets.h"
#include "AthenaStyle.h"
#include "M3Dialog.h"
#include "MaterialTheme.h"

namespace {
constexpr int AppIdRole = Qt::UserRole;

// (display label, flatpak spec) pairs offered as quick presets in the
// "Add Filesystem Access" dialog, alongside a manually-entered custom path.
const QVector<QPair<QString, QString>> &filesystemPresets()
{
    static const QVector<QPair<QString, QString>> presets = {
        {QObject::tr("Home Folder"), QStringLiteral("home")},
        {QObject::tr("Downloads"), QStringLiteral("xdg-download")},
        {QObject::tr("Documents"), QStringLiteral("xdg-documents")},
        {QObject::tr("Pictures"), QStringLiteral("xdg-pictures")},
        {QObject::tr("Music"), QStringLiteral("xdg-music")},
        {QObject::tr("Videos"), QStringLiteral("xdg-videos")},
        {QObject::tr("Entire Host Filesystem"), QStringLiteral("host")},
        {QObject::tr("Host OS Files (/usr, /etc)"), QStringLiteral("host-os")},
    };
    return presets;
}
} // namespace

PermissionsPage::PermissionsPage(FlatpakBackend *backend, QWidget *parent) : QWidget(parent), m_backend(backend)
{
    m_appList = new QListWidget(this);
    M3List::apply(m_appList, {-1, -1, /*plain=*/true});
    m_appList->setMinimumWidth(260);
    m_refreshButton = new QPushButton(AppIcons::refresh(), tr("Refresh"), this);
    MaterialTheme::styleButton(m_refreshButton, QStringLiteral("text"), QStringLiteral("refresh"));
    m_statusLabel = M3::label(tr("Loading apps..."), QStringLiteral("muted"), this);

    auto *leftLayout = new QVBoxLayout;
    // The page's own layout already pads the whole splitter.
    leftLayout->setContentsMargins(0, 0, AthenaStyle::kSidebarGap, 0);
    auto *leftTopRow = new QHBoxLayout;
    leftTopRow->addWidget(m_statusLabel, 1);
    leftTopRow->addWidget(m_refreshButton);
    leftLayout->addLayout(leftTopRow);
    leftLayout->addWidget(m_appList, 1);
    auto *leftPanel = new QWidget(this);
    leftPanel->setLayout(leftLayout);

    // Header card: the app, and resetting everything.
    auto *headerCard = M3::card(QStringLiteral("card"), this);
    auto *headerLayout = new QHBoxLayout(headerCard);
    headerLayout->setContentsMargins(20, 16, 20, 16);
    headerLayout->setSpacing(16);
    m_appIcon = new QLabel(headerCard);
    m_appIcon->setFixedSize(48, 48);
    headerLayout->addWidget(m_appIcon);
    auto *headerText = new QVBoxLayout;
    headerText->setSpacing(2);
    m_appTitle = M3::label(QString(), QStringLiteral("title"), headerCard);
    m_appTitle->setWordWrap(true);
    m_appSubtitle = M3::label(QString(), QStringLiteral("muted"), headerCard);
    m_appSubtitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
    headerText->addWidget(m_appTitle);
    headerText->addWidget(m_appSubtitle);
    headerLayout->addLayout(headerText, 1);
    m_resetButton = new QPushButton(AppIcons::clean(), tr("Reset to Defaults"), headerCard);
    MaterialTheme::styleButton(m_resetButton, QStringLiteral("danger"), QStringLiteral("restart_alt"));
    m_resetButton->setToolTip(tr("Remove every override for this app, reverting it to its default permissions."));
    headerLayout->addWidget(m_resetButton, 0, Qt::AlignVCenter);

    // Switch cards, one per kind of permission.
    auto switchCard = [this](const QString &icon, const QString &title, const QString &text, QVBoxLayout *&rows) {
        QFrame *card = M3::section(this, icon, title, text, rows);
        rows->setSpacing(0);
        return card;
    };
    QVBoxLayout *accessRows = nullptr;
    QFrame *accessGroup = switchCard(QStringLiteral("public"), tr("Access"), QString(), accessRows);
    m_networkCheck = addToggle(accessRows, QStringLiteral("wifi"), tr("Network"), tr("The internet and your local network"));
    m_ipcCheck = addToggle(accessRows, QStringLiteral("hub"), tr("Interprocess communication"),
                           tr("Shared memory with programs outside the sandbox; X11 apps usually need it"));

    QVBoxLayout *socketRows = nullptr;
    QFrame *socketsGroup = switchCard(QStringLiteral("lan"), tr("Sockets"), QString(), socketRows);
    m_waylandCheck = addToggle(socketRows, QStringLiteral("desktop_windows"), tr("Wayland windowing system"),
                               tr("Show windows through Wayland"));
    m_x11Check = addToggle(socketRows, QStringLiteral("desktop_windows"), tr("X11 windowing system"),
                           tr("Show windows through X11, which lets apps see and type into each other"));
    m_audioCheck = addToggle(socketRows, QStringLiteral("volume_up"), tr("Audio"), tr("Play and record sound (PulseAudio or PipeWire)"));
    m_sshAuthCheck = addToggle(socketRows, QStringLiteral("key"), tr("SSH agent"), tr("Use the SSH keys loaded in your agent"));
    m_sessionBusCheck = addToggle(socketRows, QStringLiteral("lan"), tr("Session bus"),
                                  tr("Talk to every service on your desktop's D-Bus session bus"));
    m_systemBusCheck = addToggle(socketRows, QStringLiteral("dns"), tr("System bus"), tr("Talk to every system service on D-Bus"));

    QVBoxLayout *deviceRows = nullptr;
    QFrame *devicesGroup = switchCard(QStringLiteral("memory"), tr("Devices"), QString(), deviceRows);
    m_gpuCheck = addToggle(deviceRows, QStringLiteral("memory"), tr("Graphics acceleration"), tr("Use the GPU (DRI) directly"));
    m_allDevicesCheck = addToggle(deviceRows, QStringLiteral("usb"), tr("All devices"),
                                  tr("Everything in /dev — webcams, game controllers, USB devices"));
    m_kvmCheck = addToggle(deviceRows, QStringLiteral("deployed_code"), tr("Virtualization"), tr("Run virtual machines (KVM)"));
    m_shmCheck = addToggle(deviceRows, QStringLiteral("storage"), tr("Shared memory"), tr("The host's /dev/shm"));

    QVBoxLayout *featureRows = nullptr;
    QFrame *featuresGroup = switchCard(QStringLiteral("tune"), tr("Features"), QString(), featureRows);
    m_develCheck = addToggle(featureRows, QStringLiteral("bug_report"), tr("Development"), tr("Debugging syscalls such as ptrace"));
    m_multiarchCheck = addToggle(featureRows, QStringLiteral("extension"), tr("Multiarch"), tr("Run programs built for other architectures (32-bit)"));
    m_bluetoothCheck = addToggle(featureRows, QStringLiteral("bluetooth"), tr("Bluetooth"), tr("Bluetooth sockets"));
    m_canbusCheck = addToggle(featureRows, QStringLiteral("cable"), tr("CAN bus"), tr("CAN networking, for automotive and industrial hardware"));
    m_perAppDevShmCheck = addToggle(featureRows, QStringLiteral("storage"), tr("Per-app /dev/shm"),
                                    tr("A private /dev/shm shared only between this app's instances"));

    // List cards: an M3 list, with add/remove actions under it.
    auto listCard = [this](const QString &icon, const QString &title, const QString &text, QAbstractItemView *view,
                           QPushButton *add, QPushButton *remove) {
        QVBoxLayout *content = nullptr;
        QFrame *card = M3::section(this, icon, title, text, content);
        view->setParent(card);
        view->setMinimumHeight(64);
        view->setMaximumHeight(240);
        content->addWidget(view);
        auto *buttons = new QHBoxLayout;
        buttons->addWidget(add);
        buttons->addWidget(remove);
        buttons->addStretch(1);
        content->addLayout(buttons);
        return card;
    };

    m_filesystemList = new QListWidget(this);
    m_filesystemList->setSelectionMode(QAbstractItemView::SingleSelection);
    M3List::apply(m_filesystemList, {-1, -1, /*plain=*/true});
    m_addFilesystemButton = new QPushButton(AppIcons::addItem(), tr("Add Access..."), this);
    MaterialTheme::styleButton(m_addFilesystemButton, QStringLiteral("tonal"), QStringLiteral("add"));
    m_removeFilesystemButton = new QPushButton(AppIcons::uninstall(), tr("Remove Selected"), this);
    MaterialTheme::styleButton(m_removeFilesystemButton, QStringLiteral("text"), QStringLiteral("delete"));
    m_removeFilesystemButton->setEnabled(false);
    QFrame *filesystemGroup = listCard(QStringLiteral("folder"), tr("Files and folders"),
                                       tr("Folders the app can open outside its sandbox, besides files you pick in a file dialog."),
                                       m_filesystemList, m_addFilesystemButton, m_removeFilesystemButton);

    m_dbusList = new QListWidget(this);
    m_dbusList->setSelectionMode(QAbstractItemView::SingleSelection);
    M3List::apply(m_dbusList, {-1, -1, /*plain=*/true});
    m_addDBusButton = new QPushButton(AppIcons::addItem(), tr("Add..."), this);
    MaterialTheme::styleButton(m_addDBusButton, QStringLiteral("tonal"), QStringLiteral("add"));
    m_removeDBusButton = new QPushButton(AppIcons::uninstall(), tr("Remove Selected"), this);
    MaterialTheme::styleButton(m_removeDBusButton, QStringLiteral("text"), QStringLiteral("delete"));
    m_removeDBusButton->setEnabled(false);
    QFrame *dbusGroup = listCard(QStringLiteral("hub"), tr("D-Bus services"),
                                 tr("Individual services the app may talk to or own, without the whole bus."),
                                 m_dbusList, m_addDBusButton, m_removeDBusButton);

    m_envTable = new QTableWidget(0, 2, this);
    m_envTable->setHorizontalHeaderLabels({tr("Key"), tr("Value")});
    m_envTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_envTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_envTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    M3List::apply(m_envTable, {/*subtitle=*/1, -1, /*plain=*/true});
    m_addEnvButton = new QPushButton(AppIcons::addItem(), tr("Add..."), this);
    MaterialTheme::styleButton(m_addEnvButton, QStringLiteral("tonal"), QStringLiteral("add"));
    m_removeEnvButton = new QPushButton(AppIcons::uninstall(), tr("Remove Selected"), this);
    MaterialTheme::styleButton(m_removeEnvButton, QStringLiteral("text"), QStringLiteral("delete"));
    m_removeEnvButton->setEnabled(false);
    QFrame *envGroup = listCard(QStringLiteral("terminal"), tr("Environment variables"),
                                tr("Variables set for the app every time it starts."), m_envTable, m_addEnvButton,
                                m_removeEnvButton);

    m_emptyState = M3::label(tr("Select an app on the left to manage its permissions."), QStringLiteral("muted"), this);
    m_emptyState->setAlignment(Qt::AlignCenter);

    auto *detailLayout = new QVBoxLayout;
    detailLayout->setContentsMargins(0, 0, 8, 8);
    detailLayout->setSpacing(16);
    detailLayout->addWidget(headerCard);
    // Two columns of switch cards when there's room for them.
    auto *switchGrid = new QGridLayout;
    switchGrid->setSpacing(16);
    switchGrid->addWidget(accessGroup, 0, 0);
    switchGrid->addWidget(devicesGroup, 1, 0);
    switchGrid->addWidget(socketsGroup, 0, 1, 2, 1);
    switchGrid->addWidget(featuresGroup, 2, 0, 1, 2);
    switchGrid->setRowStretch(1, 1);
    detailLayout->addLayout(switchGrid);
    detailLayout->addWidget(filesystemGroup);
    detailLayout->addWidget(dbusGroup);
    detailLayout->addWidget(envGroup);
    detailLayout->addStretch(1);
    m_detailPanel = new QWidget(this);
    m_detailPanel->setLayout(detailLayout);

    // Scrolls as one unit (rather than each card scrolling internally)
    // since the cards together are taller than most windows.
    auto *detailScroll = new QScrollArea(this);
    detailScroll->setWidgetResizable(true);
    detailScroll->setWidget(m_detailPanel);
    detailScroll->setFrameShape(QFrame::NoFrame);
    detailScroll->setProperty("m3", QStringLiteral("page"));

    m_detailStack = new QStackedWidget(this);
    m_detailStack->addWidget(m_emptyState);
    m_detailStack->addWidget(detailScroll);

    auto *splitter = new QSplitter(this);
    AthenaStyle::makeSidebarSplitter(splitter);
    splitter->addWidget(leftPanel);
    splitter->addWidget(m_detailStack);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({300, 900});

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(splitter, 1);

    setDetailEnabled(false);

    connect(m_refreshButton, &QPushButton::clicked, this, &PermissionsPage::refresh);
    connect(m_appList, &QListWidget::currentRowChanged, this, [this](int) { loadPermissionsForCurrentApp(); });
    connect(&m_appListWatcher, &QFutureWatcher<QVector<PackageInfo>>::finished, this,
            &PermissionsPage::onAppListLoaded);
    connect(&m_permissionsWatcher, &QFutureWatcher<FlatpakBackend::Permissions>::finished, this,
            &PermissionsPage::onPermissionsLoaded);
    connect(&m_actionWatcher, &QFutureWatcher<OperationResult>::finished, this, &PermissionsPage::onActionFinished);

    auto connectShared = [this](QAbstractButton *check, const QString &name) {
        connect(check, &QAbstractButton::toggled, this, [this, name](bool enabled) {
            if (m_updatingChecks)
                return;
            const QString appId = currentAppId();
            runAction([backend = m_backend, appId, name, enabled]() {
                return backend->setSharedEnabled(appId, name, enabled);
            });
        });
    };
    connectShared(m_networkCheck, QStringLiteral("network"));
    connectShared(m_ipcCheck, QStringLiteral("ipc"));

    auto connectSocket = [this](QAbstractButton *check, const QString &name) {
        connect(check, &QAbstractButton::toggled, this, [this, name](bool enabled) {
            if (m_updatingChecks)
                return;
            const QString appId = currentAppId();
            runAction([backend = m_backend, appId, name, enabled]() {
                return backend->setSocketEnabled(appId, name, enabled);
            });
        });
    };
    connectSocket(m_x11Check, QStringLiteral("x11"));
    connectSocket(m_waylandCheck, QStringLiteral("wayland"));
    connectSocket(m_audioCheck, QStringLiteral("pulseaudio"));
    connectSocket(m_sshAuthCheck, QStringLiteral("ssh-auth"));
    connectSocket(m_sessionBusCheck, QStringLiteral("session-bus"));
    connectSocket(m_systemBusCheck, QStringLiteral("system-bus"));

    auto connectDevice = [this](QAbstractButton *check, const QString &name) {
        connect(check, &QAbstractButton::toggled, this, [this, name](bool enabled) {
            if (m_updatingChecks)
                return;
            const QString appId = currentAppId();
            runAction([backend = m_backend, appId, name, enabled]() {
                return backend->setDeviceEnabled(appId, name, enabled);
            });
        });
    };
    connectDevice(m_gpuCheck, QStringLiteral("dri"));
    connectDevice(m_allDevicesCheck, QStringLiteral("all"));
    connectDevice(m_kvmCheck, QStringLiteral("kvm"));
    connectDevice(m_shmCheck, QStringLiteral("shm"));

    auto connectFeature = [this](QAbstractButton *check, const QString &name) {
        connect(check, &QAbstractButton::toggled, this, [this, name](bool enabled) {
            if (m_updatingChecks)
                return;
            const QString appId = currentAppId();
            runAction([backend = m_backend, appId, name, enabled]() {
                return backend->setFeatureEnabled(appId, name, enabled);
            });
        });
    };
    connectFeature(m_develCheck, QStringLiteral("devel"));
    connectFeature(m_multiarchCheck, QStringLiteral("multiarch"));
    connectFeature(m_bluetoothCheck, QStringLiteral("bluetooth"));
    connectFeature(m_canbusCheck, QStringLiteral("canbus"));
    connectFeature(m_perAppDevShmCheck, QStringLiteral("per-app-dev-shm"));

    connect(m_filesystemList, &QListWidget::currentRowChanged, this,
            [this](int row) { m_removeFilesystemButton->setEnabled(row >= 0); });

    connect(m_addFilesystemButton, &QPushButton::clicked, this, [this]() {
        const QString appId = currentAppId();
        if (appId.isEmpty())
            return;

        M3Dialog dialog(this);
        dialog.setHeadline(tr("Add Filesystem Access"), QStringLiteral("folder"));

        auto *presetCombo = new QComboBox(&dialog);
        for (const auto &preset : filesystemPresets())
            presetCombo->addItem(preset.first, preset.second);
        presetCombo->addItem(tr("Custom Folder..."), QString());

        auto *pathEdit = new QLineEdit(&dialog);
        pathEdit->setPlaceholderText(tr("/path/to/folder"));
        pathEdit->setEnabled(false);
        auto *browseButton = new QPushButton(tr("Browse..."), &dialog);
        MaterialTheme::styleButton(browseButton, QStringLiteral("text"), QStringLiteral("folder_open"));
        browseButton->setEnabled(false);

        connect(presetCombo, &QComboBox::currentIndexChanged, &dialog, [presetCombo, pathEdit, browseButton](int) {
            const bool custom = presetCombo->currentData().toString().isEmpty();
            pathEdit->setEnabled(custom);
            browseButton->setEnabled(custom);
        });
        connect(browseButton, &QPushButton::clicked, &dialog, [&dialog, pathEdit]() {
            const QString dir = QFileDialog::getExistingDirectory(&dialog, tr("Choose Folder"));
            if (!dir.isEmpty())
                pathEdit->setText(dir);
        });

        auto *readOnlyCheck = new QCheckBox(tr("Read-only"), &dialog);

        auto *pathRow = new QHBoxLayout;
        pathRow->addWidget(pathEdit, 1);
        pathRow->addWidget(browseButton);

        auto *form = new QFormLayout;
        form->addRow(tr("Location:"), presetCombo);
        form->addRow(tr("Custom Path:"), pathRow);
        form->addRow(QString(), readOnlyCheck);

        dialog.body()->addLayout(form);
        connect(dialog.addAction(tr("Cancel"), QStringLiteral("text")), &QPushButton::clicked, &dialog, &QDialog::reject);
        QPushButton *addButton = dialog.addAction(tr("Add"), QStringLiteral("filled"), QStringLiteral("add"));
        addButton->setDefault(true);
        connect(addButton, &QPushButton::clicked, &dialog, &QDialog::accept);

        if (dialog.exec() != QDialog::Accepted)
            return;

        QString spec = presetCombo->currentData().toString();
        if (spec.isEmpty())
            spec = pathEdit->text().trimmed();
        if (spec.isEmpty())
            return;
        if (readOnlyCheck->isChecked())
            spec += QStringLiteral(":ro");

        runAction([backend = m_backend, appId, spec]() { return backend->addFilesystemAccess(appId, spec); });
    });

    connect(m_removeFilesystemButton, &QPushButton::clicked, this, [this]() {
        const QString appId = currentAppId();
        QListWidgetItem *item = m_filesystemList->currentItem();
        if (appId.isEmpty() || !item)
            return;
        const QString spec = item->text();
        runAction([backend = m_backend, appId, spec]() { return backend->removeFilesystemAccess(appId, spec); });
    });

    connect(m_dbusList, &QListWidget::currentRowChanged, this,
            [this](int row) { m_removeDBusButton->setEnabled(row >= 0); });

    connect(m_addDBusButton, &QPushButton::clicked, this, [this]() {
        const QString appId = currentAppId();
        if (appId.isEmpty())
            return;

        M3Dialog dialog(this);
        dialog.setHeadline(tr("Add D-Bus Name"), QStringLiteral("hub"));

        auto *nameEdit = new QLineEdit(&dialog);
        nameEdit->setPlaceholderText(tr("org.example.Service"));
        auto *busCombo = new QComboBox(&dialog);
        busCombo->addItem(tr("Session Bus"), QStringLiteral("session"));
        busCombo->addItem(tr("System Bus"), QStringLiteral("system"));
        auto *kindCombo = new QComboBox(&dialog);
        kindCombo->addItem(tr("Talk (call methods)"), QStringLiteral("talk"));
        kindCombo->addItem(tr("Own (register the name)"), QStringLiteral("own"));

        auto *form = new QFormLayout;
        form->addRow(tr("Name:"), nameEdit);
        form->addRow(tr("Bus:"), busCombo);
        form->addRow(tr("Access:"), kindCombo);

        dialog.body()->addLayout(form);
        connect(dialog.addAction(tr("Cancel"), QStringLiteral("text")), &QPushButton::clicked, &dialog, &QDialog::reject);
        QPushButton *addButton = dialog.addAction(tr("Add"), QStringLiteral("filled"), QStringLiteral("add"));
        addButton->setDefault(true);
        connect(addButton, &QPushButton::clicked, &dialog, &QDialog::accept);

        if (dialog.exec() != QDialog::Accepted)
            return;

        const QString name = nameEdit->text().trimmed();
        if (name.isEmpty())
            return;
        const QString bus = busCombo->currentData().toString();
        const QString kind = kindCombo->currentData().toString();

        runAction([backend = m_backend, appId, bus, kind, name]() {
            return backend->grantDBusName(appId, bus, kind, name);
        });
    });

    connect(m_removeDBusButton, &QPushButton::clicked, this, [this]() {
        const QString appId = currentAppId();
        QListWidgetItem *item = m_dbusList->currentItem();
        if (appId.isEmpty() || !item)
            return;
        const QStringList data = item->data(AppIdRole).toStringList();
        const QString bus = data.value(0);
        const QString kind = data.value(1);
        const QString name = data.value(2);
        runAction([backend = m_backend, appId, bus, kind, name]() {
            return backend->revokeDBusName(appId, bus, kind, name);
        });
    });

    connect(m_envTable, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { m_removeEnvButton->setEnabled(row >= 0); });

    connect(m_addEnvButton, &QPushButton::clicked, this, [this]() {
        const QString appId = currentAppId();
        if (appId.isEmpty())
            return;

        M3Dialog dialog(this);
        dialog.setHeadline(tr("Add Environment Variable"), QStringLiteral("terminal"));

        auto *keyEdit = new QLineEdit(&dialog);
        auto *valueEdit = new QLineEdit(&dialog);

        auto *form = new QFormLayout;
        form->addRow(tr("Key:"), keyEdit);
        form->addRow(tr("Value:"), valueEdit);

        dialog.body()->addLayout(form);
        connect(dialog.addAction(tr("Cancel"), QStringLiteral("text")), &QPushButton::clicked, &dialog, &QDialog::reject);
        QPushButton *addButton = dialog.addAction(tr("Add"), QStringLiteral("filled"), QStringLiteral("add"));
        addButton->setDefault(true);
        connect(addButton, &QPushButton::clicked, &dialog, &QDialog::accept);

        if (dialog.exec() != QDialog::Accepted)
            return;

        const QString key = keyEdit->text().trimmed();
        if (key.isEmpty())
            return;
        const QString value = valueEdit->text();

        runAction([backend = m_backend, appId, key, value]() {
            return backend->setEnvironmentVariable(appId, key, value);
        });
    });

    connect(m_removeEnvButton, &QPushButton::clicked, this, [this]() {
        const QString appId = currentAppId();
        const int row = m_envTable->currentRow();
        if (appId.isEmpty() || row < 0)
            return;
        QTableWidgetItem *keyItem = m_envTable->item(row, 0);
        if (!keyItem)
            return;
        const QString key = keyItem->text();
        runAction([backend = m_backend, appId, key]() { return backend->unsetEnvironmentVariable(appId, key); });
    });

    connect(m_resetButton, &QPushButton::clicked, this, [this]() {
        const QString appId = currentAppId();
        if (appId.isEmpty())
            return;
        const auto reply = M3Message::question(
            this, tr("Reset to Defaults"),
            tr("Remove every permission override for \"%1\"?\n\nIt will revert to its default sandbox "
               "permissions.")
                .arg(appId),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes)
            return;
        runAction([backend = m_backend, appId]() { return backend->resetOverrides(appId); });
    });

    refresh();
}

M3Switch *PermissionsPage::addToggle(QVBoxLayout *rows, const QString &symbol, const QString &title, const QString &supporting)
{
    QWidget *card = rows->parentWidget();
    if (card->property("m3rows").toInt() > 0)
        rows->addWidget(M3::divider(card));
    card->setProperty("m3rows", card->property("m3rows").toInt() + 1);
    M3Switch *toggle = nullptr;
    rows->addWidget(M3::switchRow(rows->parentWidget(), symbol, title, supporting, toggle));
    return toggle;
}

void PermissionsPage::setDetailEnabled(bool enabled)
{
    m_detailPanel->setEnabled(enabled);
    if (!enabled && currentAppId().isEmpty())
        m_detailStack->setCurrentIndex(0);
}

QString PermissionsPage::currentAppId() const
{
    QListWidgetItem *item = m_appList->currentItem();
    return item ? item->data(AppIdRole).toString() : QString();
}

void PermissionsPage::refresh()
{
    if (!m_backend)
        return;

    m_refreshButton->setEnabled(false);
    m_statusLabel->setText(tr("Loading apps..."));

    FlatpakBackend *backend = m_backend;
    QFuture<QVector<PackageInfo>> future = QtConcurrent::run([backend]() { return backend->listInstalled(); });
    m_appListWatcher.setFuture(future);
}

void PermissionsPage::onAppListLoaded()
{
    const QVector<PackageInfo> apps = m_appListWatcher.result();
    const QString previouslySelected = currentAppId();

    // pkg.description is "<pretty name> — <summary>" (see
    // FlatpakBackend::listInstalled) — take just the pretty name half for
    // a readable list entry, falling back to the app ID. Sorted by that
    // display label, not the app ID, so e.g. "Firefox" files under F
    // rather than under "org.mozilla".
    QVector<QPair<QString, QString>> entries; // (label, app ID)
    for (const PackageInfo &app : apps) {
        const QString prettyName = app.description.section(QStringLiteral(" — "), 0, 0);
        entries.append({prettyName.isEmpty() ? app.name : prettyName, app.name});
    }
    std::sort(entries.begin(), entries.end(), [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
        return QString::compare(a.first, b.first, Qt::CaseInsensitive) < 0;
    });

    m_appList->clear();
    const QString wanted = !m_pendingAppId.isEmpty() ? m_pendingAppId : previouslySelected;
    m_pendingAppId.clear();
    for (const auto &[label, appId] : entries) {
        auto *item = new QListWidgetItem(label, m_appList);
        item->setData(AppIdRole, appId);
        item->setData(M3List::SubtitleRole, appId);
        if (const AppCatalog::Entry *entry = AppCatalog::find(appId); entry && !AppCatalog::iconFor(*entry).isNull())
            item->setIcon(AppCatalog::iconFor(*entry));
        else
            item->setData(M3List::SymbolRole, QStringLiteral("deployed_code"));
        if (appId == wanted)
            m_appList->setCurrentItem(item);
    }

    m_statusLabel->setText(tr("%1 apps").arg(apps.size()));
    m_refreshButton->setEnabled(true);

    if (apps.isEmpty())
        setDetailEnabled(false);
}

void PermissionsPage::selectApp(const QString &appId)
{
    for (int i = 0; i < m_appList->count(); ++i) {
        if (m_appList->item(i)->data(AppIdRole).toString() == appId) {
            m_appList->setCurrentRow(i);
            m_appList->scrollToItem(m_appList->item(i));
            return;
        }
    }
    m_pendingAppId = appId; // list not loaded yet
}

void PermissionsPage::loadPermissionsForCurrentApp()
{
    const QString appId = currentAppId();
    if (appId.isEmpty()) {
        setDetailEnabled(false);
        return;
    }

    QListWidgetItem *current = m_appList->currentItem();
    m_appTitle->setText(current->text());
    m_appSubtitle->setText(appId);
    const QIcon icon = current->icon();
    m_appIcon->setPixmap((icon.isNull() ? MaterialTheme::icon(QStringLiteral("deployed_code"), MaterialTheme::current().primary, 40)
                                        : icon).pixmap(48, 48));
    m_detailStack->setCurrentIndex(1);
    m_detailPanel->setEnabled(false); // stays disabled until the load finishes, to avoid editing stale state

    FlatpakBackend *backend = m_backend;
    QFuture<FlatpakBackend::Permissions> future =
        QtConcurrent::run([backend, appId]() { return backend->permissionsForApp(appId); });
    m_permissionsWatcher.setFuture(future);
}

void PermissionsPage::onPermissionsLoaded()
{
    if (currentAppId().isEmpty())
        return; // selection changed (or cleared) while the lookup was in flight

    applyPermissions(m_permissionsWatcher.result());
    m_detailPanel->setEnabled(true);
}

void PermissionsPage::applyPermissions(const FlatpakBackend::Permissions &perms)
{
    m_updatingChecks = true;

    m_networkCheck->setChecked(perms.shared.contains(QStringLiteral("network")));
    m_ipcCheck->setChecked(perms.shared.contains(QStringLiteral("ipc")));

    m_x11Check->setChecked(perms.sockets.contains(QStringLiteral("x11")));
    m_waylandCheck->setChecked(perms.sockets.contains(QStringLiteral("wayland")));
    m_audioCheck->setChecked(perms.sockets.contains(QStringLiteral("pulseaudio")));
    m_sshAuthCheck->setChecked(perms.sockets.contains(QStringLiteral("ssh-auth")));
    m_sessionBusCheck->setChecked(perms.sockets.contains(QStringLiteral("session-bus")));
    m_systemBusCheck->setChecked(perms.sockets.contains(QStringLiteral("system-bus")));

    m_gpuCheck->setChecked(perms.devices.contains(QStringLiteral("dri")));
    m_allDevicesCheck->setChecked(perms.devices.contains(QStringLiteral("all")));
    m_kvmCheck->setChecked(perms.devices.contains(QStringLiteral("kvm")));
    m_shmCheck->setChecked(perms.devices.contains(QStringLiteral("shm")));

    m_develCheck->setChecked(perms.features.contains(QStringLiteral("devel")));
    m_multiarchCheck->setChecked(perms.features.contains(QStringLiteral("multiarch")));
    m_bluetoothCheck->setChecked(perms.features.contains(QStringLiteral("bluetooth")));
    m_canbusCheck->setChecked(perms.features.contains(QStringLiteral("canbus")));
    m_perAppDevShmCheck->setChecked(perms.features.contains(QStringLiteral("per-app-dev-shm")));

    m_updatingChecks = false;

    rebuildFilesystemList(perms.filesystems);
    rebuildDBusList(perms);
    rebuildEnvironmentTable(perms.envVars);
}

void PermissionsPage::rebuildFilesystemList(const QStringList &filesystems)
{
    m_filesystemList->clear();
    for (const QString &spec : filesystems) {
        auto *item = new QListWidgetItem(spec, m_filesystemList);
        QString base = spec;
        QString mode = tr("Read and write");
        if (spec.endsWith(QLatin1String(":ro"))) {
            base.chop(3);
            mode = tr("Read-only");
        } else if (spec.endsWith(QLatin1String(":create"))) {
            base.chop(7);
            mode = tr("Read and write, created if missing");
        } else if (spec.endsWith(QLatin1String(":rw"))) {
            base.chop(3);
        }
        QString friendly;
        for (const auto &preset : filesystemPresets()) {
            if (preset.second == base)
                friendly = preset.first;
        }
        item->setData(M3List::SubtitleRole, friendly.isEmpty() ? mode : tr("%1 · %2").arg(friendly, mode));
        item->setData(M3List::SymbolRole, base == QLatin1String("host") || base == QLatin1String("host-os")
                                              ? QStringLiteral("warning") : QStringLiteral("folder"));
    }
    m_removeFilesystemButton->setEnabled(false);
}

void PermissionsPage::rebuildDBusList(const FlatpakBackend::Permissions &perms)
{
    m_dbusList->clear();

    auto addEntries = [this](const QStringList &names, const QString &bus, const QString &kind) {
        for (const QString &name : names) {
            const QString busLabel = bus == QLatin1String("system") ? tr("System") : tr("Session");
            const QString kindLabel = kind == QLatin1String("own") ? tr("may own the name") : tr("may talk to it");
            auto *item = new QListWidgetItem(name, m_dbusList);
            item->setData(M3List::SubtitleRole, tr("%1 bus · %2").arg(busLabel, kindLabel));
            item->setData(M3List::SymbolRole, QStringLiteral("hub"));
            item->setData(AppIdRole, QStringList{bus, kind, name});
        }
    };
    addEntries(perms.sessionBusTalk, QStringLiteral("session"), QStringLiteral("talk"));
    addEntries(perms.sessionBusOwn, QStringLiteral("session"), QStringLiteral("own"));
    addEntries(perms.systemBusTalk, QStringLiteral("system"), QStringLiteral("talk"));
    addEntries(perms.systemBusOwn, QStringLiteral("system"), QStringLiteral("own"));

    m_removeDBusButton->setEnabled(false);
}

void PermissionsPage::rebuildEnvironmentTable(const QVector<QPair<QString, QString>> &envVars)
{
    // An empty value means this app (or a previous override) set the
    // variable and it was explicitly unset via --unset-env, which itself
    // shows up as "KEY=" with no value rather than removing the entry
    // outright — that's not a variable set to an empty string, so it
    // doesn't belong in a list of variables that ARE set.
    QVector<QPair<QString, QString>> active;
    for (const auto &kv : envVars) {
        if (!kv.second.isEmpty())
            active.append(kv);
    }

    m_envTable->setRowCount(active.size());
    for (int i = 0; i < active.size(); ++i) {
        m_envTable->setItem(i, 0, new QTableWidgetItem(active.at(i).first));
        m_envTable->setItem(i, 1, new QTableWidgetItem(active.at(i).second));
    }
    m_removeEnvButton->setEnabled(false);
}

void PermissionsPage::runAction(std::function<OperationResult()> action)
{
    const QString appId = currentAppId();
    if (appId.isEmpty())
        return;

    m_detailPanel->setEnabled(false);
    QFuture<OperationResult> future = QtConcurrent::run(std::move(action));
    m_actionWatcher.setFuture(future);
}

void PermissionsPage::onActionFinished()
{
    const OperationResult result = m_actionWatcher.result();
    if (!result.success) {
        M3Message::critical(this, tr("Permission Change Failed"),
                               tr("Failed to update permissions.\n\n%1").arg(result.output.trimmed()));
    }
    // Reload regardless of success so the UI reflects the real state
    // either way, same as RepositoriesPage does after a toggle.
    loadPermissionsForCurrentApp();
}
