#pragma once

#include "../PackageBackend.h"

// Debian / Ubuntu family, via apt-cache + dpkg-query. Debian/APT has no
// direct equivalent of dnf's comps groups; the closest analog is tasksel's
// tasks, which are meta-groups (isMeta == true) backed by a `task-<id>`
// metapackage. Regular (non-meta) groups are not reported on this backend.
class AptBackend : public PackageBackend {
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

    // NOTE: unlike the rest of this backend, repository listing/toggling
    // has not been exercised against a real Debian/Ubuntu system (this
    // codebase was developed on Fedora) — it's implemented against the
    // documented sources.list / deb822 .sources formats, but treat it as
    // less battle-tested than package install/search.
    QString recentHistory() override;

    QVector<RepositoryInfo> listRepositories() override;
    OperationResult setRepositoryEnabled(const QString &repoId, bool enabled) override;

    QVector<RepositoryAddField> repositoryAddFields() const override;
    OperationResult addRepository(const RepositoryAddValues &values) override;
};
