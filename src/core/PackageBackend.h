#pragma once

#include <QString>
#include <QVector>

#include "PackageGroupInfo.h"
#include "PackageInfo.h"
#include "ProcessRunner.h"
#include "RepositoryInfo.h"

// Result of a privileged install/remove operation: whether it succeeded,
// plus the combined process output for diagnostics when it didn't (e.g. to
// show in an error dialog).
struct OperationResult {
    bool success = false;
    QString output;
};

// A human-readable, backend-formatted summary of what an install/remove
// would actually do — including pulled-in dependencies and (for removal)
// packages that would become unused — computed without applying anything,
// for showing the user before they commit to it. `available` is false
// when the backend couldn't compute one (network/lookup failure), in
// which case the caller falls back to a generic confirmation.
struct TransactionPreview {
    bool available = false;
    QString planText;
};

// Blocking, synchronous interface over a native package manager CLI.
// Callers are expected to invoke these from a worker thread (see
// ProcessRunner / QtConcurrent usage in the UI pages) since they shell out
// to external processes and can take anywhere from milliseconds to seconds
// (install/remove can take much longer, since they hit the network).
class PackageBackend {
public:
    virtual ~PackageBackend() = default;

    virtual QString backendName() const = 0;
    virtual bool isAvailable() const = 0;

    virtual QVector<PackageInfo> listInstalled() = 0;
    virtual QVector<PackageInfo> search(const QString &query) = 0;

    // "Dependency Query" search mode: packages that provide
    // (findRequires=false) or require (findRequires=true) the given
    // name/capability, instead of a plain keyword/name search. Not every
    // backend has a real, safe, non-interactive query for both directions
    // — Flatpak/Snap apps are self-contained with no comparable capability
    // graph, and Pacman has no non-interactive way to query "provides"
    // across the whole sync database — those return an empty result
    // rather than guessing.
    virtual QVector<PackageInfo> dependencyQuery(const QString &capability, bool findRequires) = 0;

    virtual QVector<PackageGroupInfo> listGroups() = 0;
    virtual PackageGroupInfo groupDetails(const QString &groupId, bool isMeta) = 0;

    // Bulk-fetches full details (installed/available version, description)
    // for an explicit set of package names in as few process calls as
    // possible, for callers that already know which names they want (e.g.
    // a group's package list) rather than searching or listing everything.
    virtual QVector<PackageInfo> packageDetails(const QStringList &packageNames) = 0;

    // Both run their underlying package manager through pkexec, which
    // prompts the user for authentication via the desktop's polkit agent.
    // Multiple packages are passed to a single package-manager invocation
    // (one transaction, one auth prompt) rather than looping per package.
    // removePackages() also removes any direct dependencies that would be
    // left unused as a result (e.g. dnf/apt "autoremove"-equivalent
    // behavior folded into the same transaction).
    virtual OperationResult installPackages(const QStringList &packageNames) = 0;
    virtual OperationResult removePackages(const QStringList &packageNames) = 0;

    // Reinstalls already-installed packages (re-fetch and re-apply the
    // same or current version) rather than a no-op "already installed".
    virtual OperationResult reinstallPackages(const QStringList &packageNames) = 0;

    // The exact command(s) installPackages()/removePackages()/
    // reinstallPackages() run, in order, for callers (TerminalOutputDialog)
    // that need to execute them one at a time while showing each command
    // line and its live output, rather than just getting a final result.
    virtual QVector<ProcessRunner::Command> installCommands(const QStringList &packageNames) const = 0;
    virtual QVector<ProcessRunner::Command> removeCommands(const QStringList &packageNames) const = 0;
    virtual QVector<ProcessRunner::Command> reinstallCommands(const QStringList &packageNames) const = 0;

    // Resolves (without applying) what installPackages()/removePackages()
    // would actually do, for a pre-flight confirmation dialog. Safe to
    // call without privilege — dependency resolution only needs to read
    // package/repo metadata, not write anything.
    virtual TransactionPreview previewInstall(const QStringList &packageNames) = 0;
    virtual TransactionPreview previewRemove(const QStringList &packageNames) = 0;

    // Removes packages that were pulled in as dependencies at some point
    // but are no longer required by anything installed (e.g. `dnf
    // autoremove`, `apt autoremove`, `pacman -R $(pacman -Qdtq)`).
    virtual OperationResult cleanUnusedDependencies() = 0;
    virtual QVector<ProcessRunner::Command> cleanUnusedDependenciesCommands() const = 0;

    // Installed packages that have a newer version available, with
    // installedVersion/availableVersion both populated so the difference
    // is visible. upgradePackages() installs the newer version for
    // already-installed packages (distinct from installPackages() since
    // some backends treat "install an installed package" and "upgrade
    // it" as different operations).
    virtual QVector<PackageInfo> listUpdates() = 0;
    virtual OperationResult upgradePackages(const QStringList &packageNames) = 0;

    // Downloads (without installing) the named packages into
    // destinationDir, optionally also fetching their not-yet-installed
    // dependencies. Where a backend has no way to redirect its download to
    // an arbitrary directory (Flatpak), destinationDir is ignored — it
    // still fetches into its own local cache so a later install of the
    // same ref is instant/offline.
    virtual QVector<ProcessRunner::Command> downloadCommands(const QStringList &packageNames,
                                                              const QString &destinationDir,
                                                              bool includeDependencies) const = 0;
    virtual OperationResult downloadPackages(const QStringList &packageNames, const QString &destinationDir,
                                              bool includeDependencies) = 0;

    // Re-syncs this backend's repository metadata/package-index cache
    // (e.g. `dnf makecache`, `apt-get update`) so packages from a
    // just-added or just-enabled repository become visible to search.
    virtual OperationResult refreshMetadata() = 0;

    // Recent install/remove/update activity, in this backend's own
    // preferred format (dnf5/flatpak have real history commands; apt/
    // pacman have no command for it, so this reads a tail of their log
    // file instead) — shown as-is in a read-only viewer rather than
    // parsed into a structured table, since the formats don't share
    // enough structure to unify cleanly.
    virtual QString recentHistory() = 0;

    virtual QVector<RepositoryInfo> listRepositories() = 0;
    virtual OperationResult setRepositoryEnabled(const QString &repoId, bool enabled) = 0;

    // Which fields a manual "add repository" dialog should collect for
    // this backend, and the handler that acts on the values it collects
    // (keyed by RepositoryAddField::key).
    virtual QVector<RepositoryAddField> repositoryAddFields() const = 0;
    virtual OperationResult addRepository(const RepositoryAddValues &values) = 0;
};
