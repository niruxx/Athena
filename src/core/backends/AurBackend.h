#pragma once

#include <QMap>
#include <QSet>
#include <QStringList>

#include "../PackageBackend.h"
#include "PacmanBackend.h"

// Arch User Repository. Package metadata (search, info, available
// versions) comes straight from the AUR's RPC v5 web API rather than any
// local database, so nothing here needs a metadata refresh. Building and
// installing goes through an AUR helper when one is installed (paru, then
// yay — both told to use pkexec instead of sudo, since there's no terminal
// for sudo to prompt in), and otherwise through Athena's own minimal
// fallback: clone each package's git repo from aur.archlinux.org into the
// user cache and run `makepkg -si` there, with makepkg's own PACMAN_AUTH
// hook pointed at pkexec for its install/dependency steps. That fallback
// resolves AUR-only dependencies itself (building them first, installed
// --asdeps); repository dependencies are left to makepkg --syncdeps.
//
// Everything that acts on an already-installed package (remove, orphan
// cleanup) is plain pacman, so those delegate to PacmanBackend.
class AurBackend : public PackageBackend {
public:
    // Which tool installCommands()/reinstallCommands() will drive.
    enum class BuildMethod { Paru, Yay, Makepkg };

    // One AUR package's RPC "info" record, trimmed to the fields Athena
    // uses.
    struct AurPackage {
        QString name;
        QString packageBase;
        QString version;
        QString description;
        QString url;
        QString maintainer; // empty means orphaned
        int numVotes = 0;
        double popularity = 0.0;
        qint64 outOfDate = 0; // unix time flagged out-of-date, 0 if not
        QStringList depends;
        QStringList makeDepends;
        QStringList checkDepends;
    };

    QString backendName() const override;
    bool isAvailable() const override;

    static BuildMethod buildMethod();
    static QString buildMethodName(BuildMethod method);

    // Bootstraps an AUR helper ("yay" or "paru") the way both projects
    // document it: make sure git and base-devel are present, clone the
    // helper's official AUR git repo, and `makepkg -si` it. Empty for any
    // other name. Once installed, buildMethod() picks the helper up on the
    // next build — nothing needs restarting.
    static QVector<ProcessRunner::Command> helperInstallCommands(const QString &helper);

    // The installed package that provides the `helper` executable — not
    // always the helper's own name, since yay-bin, paru-git etc. provide
    // the same command. Empty if the helper isn't installed (or its binary
    // isn't owned by any package, e.g. a manual install into ~/.local).
    static QString helperOwningPackage(const QString &helper);
    // Uninstalls that package (plus dependencies nothing else needs, like
    // pacman -Rs elsewhere in Athena). Empty if there's nothing to remove.
    static QVector<ProcessRunner::Command> helperRemoveCommands(const QString &helper);

    // Whether AUR builds must go through the PKGBUILD review dialog
    // (AppSettings::reviewAurBuilds()). While it's on, every unreviewed
    // build path here — installCommands(), reinstallCommands(),
    // upgradePackages() and friends — fails closed with an error instead
    // of building, so nothing can skip review by accident.
    static bool reviewRequired();

    // A build that's been resolved and fetched but not run yet: each AUR
    // package base to build (dependencies first), freshly cloned into its
    // own directory under the user cache, for the user to review and edit
    // in place. reviewedBuildCommands() then builds exactly those
    // directories — nothing is re-downloaded between review and build.
    struct PreparedBuild {
        struct Package {
            AurPackage info;
            QString directory;
            bool asDependency = false;
        };
        QVector<Package> packages;
        QStringList repoDependencies;
        QStringList unresolved;
        QStringList notFound;
        QString error; // set if resolving or cloning failed; nothing to build then
    };
    // Blocking (network + git) — call from a worker thread.
    static PreparedBuild prepareReviewedBuild(const QStringList &packageNames);
    static QVector<ProcessRunner::Command> reviewedBuildCommands(const PreparedBuild &build, bool reinstall);

    // Fetches the PKGBUILD for packageName (resolving it to its package
    // base first, since split packages share one). Returns an empty string
    // and sets errorOut on failure. Blocking — call from a worker thread.
    QString fetchPkgbuild(const QString &packageName, QString *errorOut = nullptr) const;

    QVector<PackageInfo> listInstalled() override;
    QVector<PackageInfo> search(const QString &query) override;
    QVector<PackageInfo> dependencyQuery(const QString &capability, bool findRequires) override;

    // The AUR has no package groups.
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

    // Installed foreign packages whose AUR version is newer (per vercmp).
    // VCS packages (-git etc.) only show up when their PKGBUILD's pkgver
    // was bumped on the AUR, not for every new upstream commit.
    QVector<PackageInfo> listUpdates() override;
    OperationResult upgradePackages(const QStringList &packageNames) override;
    OperationResult refreshMetadata() override;

    // Clones each package's AUR git repo (PKGBUILD and friends) into
    // destinationDir/<pkgbase> rather than downloading a built package —
    // the AUR only ever distributes build scripts. includeDependencies
    // also clones any AUR-only dependencies.
    QVector<ProcessRunner::Command> downloadCommands(const QStringList &packageNames, const QString &destinationDir,
                                                      bool includeDependencies) const override;
    OperationResult downloadPackages(const QStringList &packageNames, const QString &destinationDir,
                                      bool includeDependencies) override;

    // pacman.log entries for AUR/locally-built packages: anything from a
    // `pacman -U` transaction (how makepkg and helpers install) plus any
    // entry naming a currently-installed foreign package.
    QString recentHistory() override;

    // The AUR is a single, fixed source; there are no repositories to list
    // or add.
    QVector<RepositoryInfo> listRepositories() override;
    OperationResult setRepositoryEnabled(const QString &repoId, bool enabled) override;
    QVector<RepositoryAddField> repositoryAddFields() const override;
    OperationResult addRepository(const RepositoryAddValues &values) override;

private:
    // The AUR packages that need building to install `targets`, in build
    // order (dependencies first), plus the repository packages makepkg
    // will pull in along the way. Targets are always included even if
    // already installed; AUR dependencies only if not already satisfied.
    struct BuildPlan {
        QVector<AurPackage> toBuild;
        QSet<QString> dependencyNames; // members of toBuild pulled in as deps, not requested
        QStringList repoDependencies;
        QStringList unresolved; // neither installed, in a sync repo, nor on the AUR
        QStringList notFound;   // requested targets that don't exist on the AUR
    };
    static BuildPlan resolveBuildPlan(const QStringList &targets);

    QVector<ProcessRunner::Command> buildCommands(const QStringList &packageNames, bool reinstall) const;

    // Name -> installed version for every foreign (`pacman -Qm`) package.
    static QMap<QString, QString> installedForeignPackages();

    mutable PacmanBackend m_pacman;
};
