#include "PermissionsPage.h"

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
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "AppIcons.h"

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
    m_refreshButton = new QPushButton(AppIcons::refresh(), tr("Refresh"), this);
    m_statusLabel = new QLabel(tr("Loading apps..."), this);

    auto *leftLayout = new QVBoxLayout;
    auto *leftTopRow = new QHBoxLayout;
    leftTopRow->addWidget(m_statusLabel, 1);
    leftTopRow->addWidget(m_refreshButton);
    leftLayout->addLayout(leftTopRow);
    leftLayout->addWidget(m_appList, 1);
    auto *leftPanel = new QWidget(this);
    leftPanel->setLayout(leftLayout);

    m_appTitle = new QLabel(tr("Select an app on the left to manage its permissions."), this);
    QFont titleFont = m_appTitle->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    m_appTitle->setFont(titleFont);
    m_appTitle->setWordWrap(true);

    auto *accessGroup = new QGroupBox(tr("Access"), this);
    auto *accessLayout = new QHBoxLayout(accessGroup);
    m_networkCheck = new QCheckBox(tr("Network"), this);
    m_ipcCheck = new QCheckBox(tr("Interprocess Communication"), this);
    accessLayout->addWidget(m_networkCheck);
    accessLayout->addWidget(m_ipcCheck);
    accessLayout->addStretch(1);

    auto *socketsGroup = new QGroupBox(tr("Sockets"), this);
    auto *socketsGrid = new QGridLayout(socketsGroup);
    m_x11Check = addToggle(socketsGrid, 0, 0, tr("X11 Windowing System"));
    m_waylandCheck = addToggle(socketsGrid, 1, 0, tr("Wayland Windowing System"));
    m_audioCheck = addToggle(socketsGrid, 2, 0, tr("Audio (PulseAudio)"));
    m_sshAuthCheck = addToggle(socketsGrid, 0, 1, tr("SSH Agent"));
    m_sessionBusCheck = addToggle(socketsGrid, 1, 1, tr("Session Bus (D-Bus)"));
    m_systemBusCheck = addToggle(socketsGrid, 2, 1, tr("System Bus (D-Bus)"));

    auto *devicesGroup = new QGroupBox(tr("Devices"), this);
    auto *devicesGrid = new QGridLayout(devicesGroup);
    m_gpuCheck = addToggle(devicesGrid, 0, 0, tr("Graphics (GPU/DRI)"));
    m_allDevicesCheck = addToggle(devicesGrid, 1, 0, tr("All Devices"));
    m_kvmCheck = addToggle(devicesGrid, 0, 1, tr("Virtualization (KVM)"));
    m_shmCheck = addToggle(devicesGrid, 1, 1, tr("Shared Memory"));

    auto *featuresGroup = new QGroupBox(tr("Features"), this);
    auto *featuresGrid = new QGridLayout(featuresGroup);
    m_develCheck = addToggle(featuresGrid, 0, 0, tr("Development (ptrace, etc.)"));
    m_multiarchCheck = addToggle(featuresGrid, 1, 0, tr("Multiarch"));
    m_bluetoothCheck = addToggle(featuresGrid, 0, 1, tr("Bluetooth"));
    m_canbusCheck = addToggle(featuresGrid, 1, 1, tr("CAN Bus"));
    m_perAppDevShmCheck = addToggle(featuresGrid, 2, 0, tr("Per-application /dev/shm"));

    auto *filesystemGroup = new QGroupBox(tr("Filesystem Access"), this);
    m_filesystemList = new QListWidget(this);
    m_filesystemList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_addFilesystemButton = new QPushButton(AppIcons::addItem(), tr("Add Access..."), this);
    m_removeFilesystemButton = new QPushButton(AppIcons::uninstall(), tr("Remove Selected"), this);
    m_removeFilesystemButton->setEnabled(false);

    auto *filesystemButtonRow = new QHBoxLayout;
    filesystemButtonRow->addWidget(m_addFilesystemButton);
    filesystemButtonRow->addWidget(m_removeFilesystemButton);
    filesystemButtonRow->addStretch(1);

    auto *filesystemLayout = new QVBoxLayout(filesystemGroup);
    filesystemLayout->addWidget(m_filesystemList, 1);
    filesystemLayout->addLayout(filesystemButtonRow);

    auto *dbusGroup = new QGroupBox(tr("D-Bus Access"), this);
    m_dbusList = new QListWidget(this);
    m_dbusList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_addDBusButton = new QPushButton(AppIcons::addItem(), tr("Add..."), this);
    m_removeDBusButton = new QPushButton(AppIcons::uninstall(), tr("Remove Selected"), this);
    m_removeDBusButton->setEnabled(false);

    auto *dbusButtonRow = new QHBoxLayout;
    dbusButtonRow->addWidget(m_addDBusButton);
    dbusButtonRow->addWidget(m_removeDBusButton);
    dbusButtonRow->addStretch(1);

    auto *dbusLayout = new QVBoxLayout(dbusGroup);
    dbusLayout->addWidget(m_dbusList, 1);
    dbusLayout->addLayout(dbusButtonRow);

    auto *envGroup = new QGroupBox(tr("Environment Variables"), this);
    m_envTable = new QTableWidget(0, 2, this);
    m_envTable->setHorizontalHeaderLabels({tr("Key"), tr("Value")});
    m_envTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_envTable->verticalHeader()->setVisible(false);
    m_envTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_envTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_envTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_addEnvButton = new QPushButton(AppIcons::addItem(), tr("Add..."), this);
    m_removeEnvButton = new QPushButton(AppIcons::uninstall(), tr("Remove Selected"), this);
    m_removeEnvButton->setEnabled(false);

    auto *envButtonRow = new QHBoxLayout;
    envButtonRow->addWidget(m_addEnvButton);
    envButtonRow->addWidget(m_removeEnvButton);
    envButtonRow->addStretch(1);

    auto *envLayout = new QVBoxLayout(envGroup);
    envLayout->addWidget(m_envTable, 1);
    envLayout->addLayout(envButtonRow);

    m_resetButton = new QPushButton(AppIcons::clean(), tr("Reset to Defaults"), this);
    m_resetButton->setToolTip(tr("Remove every override for this app, reverting it to its default permissions."));

    auto *resetRow = new QHBoxLayout;
    resetRow->addStretch(1);
    resetRow->addWidget(m_resetButton);

    auto *detailLayout = new QVBoxLayout;
    detailLayout->addWidget(m_appTitle);
    detailLayout->addWidget(accessGroup);
    detailLayout->addWidget(socketsGroup);
    detailLayout->addWidget(devicesGroup);
    detailLayout->addWidget(featuresGroup);
    detailLayout->addWidget(filesystemGroup);
    detailLayout->addWidget(dbusGroup);
    detailLayout->addWidget(envGroup);
    detailLayout->addLayout(resetRow);
    m_detailPanel = new QWidget(this);
    m_detailPanel->setLayout(detailLayout);

    // Scrolls as one unit (rather than each group box scrolling
    // internally) since the groups together are taller than most windows
    // once features/D-Bus/environment are all shown at once.
    auto *detailScroll = new QScrollArea(this);
    detailScroll->setWidgetResizable(true);
    detailScroll->setWidget(m_detailPanel);
    detailScroll->setFrameShape(QFrame::NoFrame);

    auto *splitter = new QSplitter(this);
    splitter->addWidget(leftPanel);
    splitter->addWidget(detailScroll);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({280, 720});

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

    auto connectShared = [this](QCheckBox *check, const QString &name) {
        connect(check, &QCheckBox::toggled, this, [this, name](bool enabled) {
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

    auto connectSocket = [this](QCheckBox *check, const QString &name) {
        connect(check, &QCheckBox::toggled, this, [this, name](bool enabled) {
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

    auto connectDevice = [this](QCheckBox *check, const QString &name) {
        connect(check, &QCheckBox::toggled, this, [this, name](bool enabled) {
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

    auto connectFeature = [this](QCheckBox *check, const QString &name) {
        connect(check, &QCheckBox::toggled, this, [this, name](bool enabled) {
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

        QDialog dialog(this);
        dialog.setWindowTitle(tr("Add Filesystem Access"));

        auto *presetCombo = new QComboBox(&dialog);
        for (const auto &preset : filesystemPresets())
            presetCombo->addItem(preset.first, preset.second);
        presetCombo->addItem(tr("Custom Folder..."), QString());

        auto *pathEdit = new QLineEdit(&dialog);
        pathEdit->setPlaceholderText(tr("/path/to/folder"));
        pathEdit->setEnabled(false);
        auto *browseButton = new QPushButton(tr("Browse..."), &dialog);
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

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        auto *dialogLayout = new QVBoxLayout(&dialog);
        dialogLayout->addLayout(form);
        dialogLayout->addWidget(buttons);

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

        QDialog dialog(this);
        dialog.setWindowTitle(tr("Add D-Bus Name"));

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

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        auto *dialogLayout = new QVBoxLayout(&dialog);
        dialogLayout->addLayout(form);
        dialogLayout->addWidget(buttons);

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

        QDialog dialog(this);
        dialog.setWindowTitle(tr("Add Environment Variable"));

        auto *keyEdit = new QLineEdit(&dialog);
        auto *valueEdit = new QLineEdit(&dialog);

        auto *form = new QFormLayout;
        form->addRow(tr("Key:"), keyEdit);
        form->addRow(tr("Value:"), valueEdit);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        auto *dialogLayout = new QVBoxLayout(&dialog);
        dialogLayout->addLayout(form);
        dialogLayout->addWidget(buttons);

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
        const auto reply = QMessageBox::question(
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

QCheckBox *PermissionsPage::addToggle(QGridLayout *grid, int row, int column, const QString &label)
{
    auto *check = new QCheckBox(label, this);
    grid->addWidget(check, row, column);
    return check;
}

void PermissionsPage::setDetailEnabled(bool enabled)
{
    m_detailPanel->setEnabled(enabled);
    if (!enabled)
        m_appTitle->setText(tr("Select an app on the left to manage its permissions."));
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

    m_appList->clear();
    for (const PackageInfo &app : apps) {
        // pkg.description is "<pretty name> — <summary>" (see
        // FlatpakBackend::listInstalled) — take just the pretty name half
        // for a readable list entry, falling back to the app ID.
        const QString prettyName = app.description.section(QStringLiteral(" — "), 0, 0);
        auto *item = new QListWidgetItem(
            prettyName.isEmpty() ? app.name : tr("%1 (%2)").arg(prettyName, app.name), m_appList);
        item->setData(AppIdRole, app.name);
        if (app.name == previouslySelected)
            m_appList->setCurrentItem(item);
    }

    m_statusLabel->setText(tr("%1 apps").arg(apps.size()));
    m_refreshButton->setEnabled(true);

    if (apps.isEmpty())
        setDetailEnabled(false);
}

void PermissionsPage::loadPermissionsForCurrentApp()
{
    const QString appId = currentAppId();
    if (appId.isEmpty()) {
        setDetailEnabled(false);
        return;
    }

    m_appTitle->setText(m_appList->currentItem()->text());
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
    m_filesystemList->addItems(filesystems);
    m_removeFilesystemButton->setEnabled(false);
}

void PermissionsPage::rebuildDBusList(const FlatpakBackend::Permissions &perms)
{
    m_dbusList->clear();

    auto addEntries = [this](const QStringList &names, const QString &bus, const QString &kind) {
        for (const QString &name : names) {
            const QString busLabel = bus == QLatin1String("system") ? tr("system") : tr("session");
            const QString kindLabel = kind == QLatin1String("own") ? tr("own") : tr("talk");
            auto *item =
                new QListWidgetItem(tr("[%1 bus, %2] %3").arg(busLabel, kindLabel, name), m_dbusList);
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
        QMessageBox::critical(this, tr("Permission Change Failed"),
                               tr("Failed to update permissions.\n\n%1").arg(result.output.trimmed()));
    }
    // Reload regardless of success so the UI reflects the real state
    // either way, same as RepositoriesPage does after a toggle.
    loadPermissionsForCurrentApp();
}
