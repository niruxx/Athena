#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ProcessRunner.h"

// System-level helpers beyond plain package transactions, keyed by the
// native package manager ("pacman", "dnf", "apt"): snapshots before
// transactions, package-cache cleanup, mirror ranking, downgrades, Arch
// news, a cross-source activity timeline, and exporting/importing the set
// of installed packages. Everything that changes the system is returned as
// commands (run through pkexec in a TerminalOutputDialog) rather than run
// here, so the user always sees exactly what will run first.
namespace SystemTools {

// ---- Snapshots (Snapper or Timeshift) -------------------------------------

enum class SnapshotTool { None, Snapper, Timeshift };
SnapshotTool snapshotTool();
QString snapshotToolName(SnapshotTool tool);
// The command that takes a snapshot labeled with `description`, or none.
QVector<ProcessRunner::Command> snapshotCommands(const QString &description);
struct Snapshot {
    QString id;          // Snapper's number, or Timeshift's snapshot name
    QDateTime time;
    QString description;
    QString kind;        // Snapper: "single", "pre", "post"; Timeshift: its tags ("O", "B", "D", ...)
    QString pairedWith;  // Snapper: a post snapshot's pre number
    QString user;        // Snapper: who took it
};
// Most recent first, at most `limit` (none for no limit). Snapper lists
// unprivileged when its config allows the user; Timeshift needs root to
// list at all, so this is empty for it unless asAdmin (which prompts).
QVector<Snapshot> listSnapshots(int limit = 5, bool asAdmin = false, QString *error = nullptr);
QVector<Snapshot> parseSnapperJson(const QString &json);
QVector<Snapshot> parseTimeshiftList(const QString &output);

// Deleting a snapshot, and restoring one. Snapper can restore two ways:
// rolling back (the next boot starts from a writable copy of the
// snapshot — needs a Btrfs root that boots its default subvolume) or
// undoing every file change made since the snapshot, in place. Timeshift
// restores the files and then restarts the computer.
enum class RestoreMode { Rollback, UndoChanges };
QVector<ProcessRunner::Command> deleteSnapshotCommands(const Snapshot &snapshot);
QVector<ProcessRunner::Command> restoreSnapshotCommands(const Snapshot &snapshot, RestoreMode mode = RestoreMode::Rollback);

// ---- Package cache --------------------------------------------------------

QString packageCacheDir(const QString &nativeKey);
qint64 directorySize(const QString &path);
struct CacheCleanup {
    QString label;   // what the option removes, for a checkbox
    QString detail;  // how, in a line
    QVector<ProcessRunner::Command> commands;
};
// The cleanup options for this package manager: old versions (keeping
// `keepVersions`) and uninstalled packages via paccache when pacman-contrib
// is installed, else pacman -Sc; dnf/apt clean.
QVector<CacheCleanup> cacheCleanupOptions(const QString &nativeKey, int keepVersions = 2);

// ---- Mirrors ---------------------------------------------------------------

bool mirrorRankingAvailable(const QString &nativeKey);
// What's needed before ranking can run (e.g. reflector), or empty.
QString mirrorRankingRequirement(const QString &nativeKey);
// Ranks into a user-owned temporary file first (unprivileged), so the new
// list can be shown before anything is replaced.
QVector<ProcessRunner::Command> rankMirrorsCommands(const QString &nativeKey, const QString &outputFile);
// Backs up the current list and installs `rankedFile` in its place.
QVector<ProcessRunner::Command> applyMirrorsCommands(const QString &nativeKey, const QString &rankedFile);
QString mirrorListPath(const QString &nativeKey);

// ---- Downgrade (Arch) --------------------------------------------------------

struct PackageVersion {
    QString version;
    QString location; // local file path or archive URL
    bool cached = false;
};
// Versions of `package` other than `installedVersion`: from the local pacman
// cache, then from the Arch Linux Archive (network). Newest first.
QVector<PackageVersion> downgradeCandidates(const QString &package, const QString &installedVersion);
QVector<ProcessRunner::Command> installVersionCommands(const PackageVersion &version);

// ---- Arch news ----------------------------------------------------------------

struct NewsItem {
    QString title;
    QString link;
    QDateTime published;
    QString html; // the post body
    // Arch flags posts that need manual action before upgrading.
    bool manualIntervention = false;
};
QVector<NewsItem> archNews(int limit = 8);
// When pacman last ran a full system upgrade, from pacman.log.
QDateTime lastSystemUpgrade();

// ---- Activity timeline ----------------------------------------------------------

struct Activity {
    QDateTime time;
    QString action; // "installed", "upgraded", "removed", "reinstalled", "downgraded"
    QString package;
    QString detail; // e.g. "1.2-1 → 1.3-1"
    QString sourceKey;
};
// Recent install/upgrade/remove activity from the native package manager's
// log, plus Flatpak's and Snap's history when `extraSources` lists
// "flatpak" / "snap", newest first.
QVector<Activity> recentActivity(const QString &nativeKey, const QStringList &extraSources, int limit = 40);

// ---- Export / import ----------------------------------------------------------------

// Explicitly installed (not pulled in as dependencies) native packages.
QStringList explicitlyInstalled(const QString &nativeKey);

} // namespace SystemTools
