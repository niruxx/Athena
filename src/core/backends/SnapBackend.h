#pragma once

#include "../PackageBackend.h"

// Snap, via the `snap` CLI. Like Flatpak, this is offered as an additional
// tab alongside whichever native backend BackendFactory picked (or on its
// own if none was found), not chosen by it — snapd is available on most
// distros regardless of the native package manager.
//
// Snap has no comps-style groups and no repository/remote concept (one
// global store), so listGroups()/groupDetails()/listRepositories()/
// repositoryAddFields() are unsupported (return empty) and
// setRepositoryEnabled()/addRepository() fail cleanly if ever called.
//
// Unlike Flatpak, the classic `snap` client has no built-in polkit
// integration of its own — install/remove/refresh genuinely need root, so
// those go through pkexec like the native distro backends do.
class SnapBackend : public PackageBackend {
public:
    QString backendName() const override;
    bool isAvailable() const override;

    QVector<PackageInfo> listInstalled() override;
    QVector<PackageInfo> search(const QString &query) override;

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

    QString recentHistory() override;

    QVector<RepositoryInfo> listRepositories() override;
    OperationResult setRepositoryEnabled(const QString &repoId, bool enabled) override;

    QVector<RepositoryAddField> repositoryAddFields() const override;
    OperationResult addRepository(const RepositoryAddValues &values) override;
};
