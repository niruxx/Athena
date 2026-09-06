#pragma once

#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

#include "../PackageBackend.h"

// Flatpak, via the `flatpak` CLI. Independent of the distro's native
// package manager — this backend is offered as an additional tab
// alongside whichever native backend BackendFactory picked, not chosen by
// it. Flatpak has no comps-style groups, so listGroups()/groupDetails()
// are unsupported (return empty).
//
// Unlike the native backends, install/remove is NOT wrapped in pkexec:
// flatpak ships its own polkit action (org.freedesktop.Flatpak.policy) and
// D-Bus system helper, so a plain `flatpak install` invoked as a regular
// user already triggers the native authentication prompt itself for
// system-wide installs.
class FlatpakBackend : public PackageBackend {
public:
    QString backendName() const override;
    bool isAvailable() const override;

    QVector<PackageInfo> listInstalled() override;
    QVector<PackageInfo> search(const QString &query) override;
    QVector<PackageInfo> dependencyQuery(const QString &capability, bool findRequires) override;

    QVector<PackageGroupInfo> listGroups() override;
    PackageGroupInfo groupDetails(const QString &groupId, bool isMeta) override;

    QVector<PackageInfo> packageDetails(const QStringList &packageNames) override;

    OperationResult installPackages(const QStringList &packageNames) override;
    OperationResult removePackages(const QStringList &packageNames) override;
    OperationResult reinstallPackages(const QStringList &packageNames) override;

    QVector<ProcessRunner::Command> installCommands(const QStringList &packageNames) const override;
    QVector<ProcessRunner::Command> removeCommands(const QStringList &packageNames) const override;
    QVector<ProcessRunner::Command> reinstallCommands(const QStringList &packageNames) const override;

    TransactionPreview previewInstall(const QStringList &packageNames) override;
    TransactionPreview previewRemove(const QStringList &packageNames) override;
    OperationResult cleanUnusedDependencies() override;
    QVector<ProcessRunner::Command> cleanUnusedDependenciesCommands() const override;

    QVector<PackageInfo> listUpdates() override;
    OperationResult upgradePackages(const QStringList &packageNames) override;
    OperationResult refreshMetadata() override;

    QVector<ProcessRunner::Command> downloadCommands(const QStringList &packageNames, const QString &destinationDir,
                                                      bool includeDependencies) const override;
    OperationResult downloadPackages(const QStringList &packageNames, const QString &destinationDir,
                                      bool includeDependencies) override;

    QString recentHistory() override;

    QVector<RepositoryInfo> listRepositories() override;
    OperationResult setRepositoryEnabled(const QString &repoId, bool enabled) override;

    QVector<RepositoryAddField> repositoryAddFields() const override;
    OperationResult addRepository(const RepositoryAddValues &values) override;

    // Flatpak's per-app sandbox overrides (`flatpak override`) — a
    // Flatpak-only concept with nothing corresponding in PackageBackend,
    // so these live directly on this class instead of the shared
    // interface. Everything here acts on user-level overrides
    // (~/.local/share/flatpak/overrides), which need no elevated
    // privileges and apply regardless of whether a given app itself is
    // installed per-user or system-wide.
    struct Permissions {
        QSet<QString> shared;    // e.g. "network", "ipc"
        QSet<QString> sockets;   // e.g. "x11", "wayland", "pulseaudio", "session-bus", "system-bus", "ssh-auth"
        QSet<QString> devices;   // e.g. "dri", "all", "kvm", "shm"
        QSet<QString> features;  // e.g. "devel", "multiarch", "bluetooth", "canbus", "per-app-dev-shm"
        QStringList filesystems; // raw entries as reported, e.g. "home", "host", "xdg-download:ro"
        QStringList sessionBusTalk;
        QStringList sessionBusOwn;
        QStringList systemBusTalk;
        QStringList systemBusOwn;
        QVector<QPair<QString, QString>> envVars; // KEY -> VALUE
    };

    Permissions permissionsForApp(const QString &appId) const;
    OperationResult setSharedEnabled(const QString &appId, const QString &name, bool enabled);
    OperationResult setSocketEnabled(const QString &appId, const QString &name, bool enabled);
    OperationResult setDeviceEnabled(const QString &appId, const QString &name, bool enabled);
    OperationResult setFeatureEnabled(const QString &appId, const QString &name, bool enabled);
    OperationResult addFilesystemAccess(const QString &appId, const QString &pathSpec);
    OperationResult removeFilesystemAccess(const QString &appId, const QString &pathSpec);
    OperationResult setEnvironmentVariable(const QString &appId, const QString &key, const QString &value);
    OperationResult unsetEnvironmentVariable(const QString &appId, const QString &key);
    // bus is "session" or "system"; kind is "talk" or "own".
    OperationResult grantDBusName(const QString &appId, const QString &bus, const QString &kind,
                                  const QString &name);
    OperationResult revokeDBusName(const QString &appId, const QString &bus, const QString &kind,
                                    const QString &name);
    OperationResult resetOverrides(const QString &appId);

    // Where Flatpak keeps per-app user data and user-level permission
    // overrides — used by the User Data / Leftover Data tabs, which work
    // directly with these directories rather than through `flatpak`
    // subcommands (there is no CLI query for either).
    static QString userDataRoot();      // ~/.var/app
    static QString userOverridesRoot(); // ~/.local/share/flatpak/overrides

private:
    // Which remote a given application ID should be installed from;
    // queried fresh each time rather than cached, since this backend
    // instance is shared across concurrently-running background tasks.
    QString resolveRemoteForAppId(const QString &appId) const;
};
