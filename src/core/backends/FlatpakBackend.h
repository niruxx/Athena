#pragma once

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

private:
    // Which remote a given application ID should be installed from;
    // queried fresh each time rather than cached, since this backend
    // instance is shared across concurrently-running background tasks.
    QString resolveRemoteForAppId(const QString &appId) const;
};
