#pragma once

#include <functional>

#include <QFutureWatcher>
#include <QWidget>

#include "../core/backends/FlatpakBackend.h"

class QListWidget;
class QListWidgetItem;
class QCheckBox;
class QLabel;
class QPushButton;
class QGridLayout;
class QTableWidget;

// Lets the user inspect and edit an installed Flatpak app's sandbox
// permissions (`flatpak override --user`): shared namespaces, exposed
// sockets/devices/features, filesystem access, D-Bus name policies, and
// environment variables. This is a Flatpak-only concept with no
// equivalent on other backends, so unlike the other tabs it takes a
// FlatpakBackend* directly rather than the generic PackageBackend
// interface.
class PermissionsPage : public QWidget {
    Q_OBJECT

public:
    explicit PermissionsPage(FlatpakBackend *backend, QWidget *parent = nullptr);

public slots:
    void refresh();

private slots:
    void onAppListLoaded();
    void onPermissionsLoaded();
    void onActionFinished();

private:
    void loadPermissionsForCurrentApp();
    void applyPermissions(const FlatpakBackend::Permissions &perms);
    void rebuildFilesystemList(const QStringList &filesystems);
    void rebuildDBusList(const FlatpakBackend::Permissions &perms);
    void rebuildEnvironmentTable(const QVector<QPair<QString, QString>> &envVars);
    void setDetailEnabled(bool enabled);
    QString currentAppId() const;
    QCheckBox *addToggle(QGridLayout *grid, int row, int column, const QString &label);

    // Runs a mutating override call (checkbox toggle, filesystem add/
    // remove, reset) off the UI thread, then reloads the current app's
    // permissions so the UI reflects what actually took effect.
    void runAction(std::function<OperationResult()> action);

    FlatpakBackend *m_backend;

    QListWidget *m_appList;
    QPushButton *m_refreshButton;
    QLabel *m_statusLabel;

    QWidget *m_detailPanel;
    QLabel *m_appTitle;

    QCheckBox *m_networkCheck;
    QCheckBox *m_ipcCheck;

    QCheckBox *m_x11Check;
    QCheckBox *m_waylandCheck;
    QCheckBox *m_audioCheck;
    QCheckBox *m_sshAuthCheck;
    QCheckBox *m_sessionBusCheck;
    QCheckBox *m_systemBusCheck;

    QCheckBox *m_gpuCheck;
    QCheckBox *m_allDevicesCheck;
    QCheckBox *m_kvmCheck;
    QCheckBox *m_shmCheck;

    QCheckBox *m_develCheck;
    QCheckBox *m_multiarchCheck;
    QCheckBox *m_bluetoothCheck;
    QCheckBox *m_canbusCheck;
    QCheckBox *m_perAppDevShmCheck;

    QListWidget *m_filesystemList;
    QPushButton *m_addFilesystemButton;
    QPushButton *m_removeFilesystemButton;

    QListWidget *m_dbusList;
    QPushButton *m_addDBusButton;
    QPushButton *m_removeDBusButton;

    QTableWidget *m_envTable;
    QPushButton *m_addEnvButton;
    QPushButton *m_removeEnvButton;

    QPushButton *m_resetButton;

    // Suppresses the checkboxes' toggled() handlers while
    // applyPermissions() sets their state to match a freshly (re)loaded
    // app, so redisplaying current state doesn't re-trigger an override.
    bool m_updatingChecks = false;

    QFutureWatcher<QVector<PackageInfo>> m_appListWatcher;
    QFutureWatcher<FlatpakBackend::Permissions> m_permissionsWatcher;
    QFutureWatcher<OperationResult> m_actionWatcher;
};
