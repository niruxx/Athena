#pragma once

#include <functional>

#include <QString>
#include <QStringList>

#include "../core/PackageBackend.h"

class QWidget;

// Shared confirm -> run -> report-outcome flow for the privileged
// install/remove/reinstall/cleanup actions, used by PackageBrowser and
// InstalledPage (and so, by extension, InstalledPage/SearchPage/GroupsPage)
// so the pkexec + terminal-dialog plumbing isn't duplicated per page.
namespace PackageActions {

// Generic primitive: prompts for confirmation, then (if confirmed) runs
// `operation` on a worker thread. onFinished(success) is always called back
// on the calling (UI) thread exactly once — either with false immediately
// if the user declines the confirmation prompt, or with the operation's
// actual result once it completes. Callers that set a busy/disabled state
// before calling this can safely clear it from onFinished alone. Used
// directly for operations that are more than a plain package-name batch
// (e.g. GroupsPage expanding a selected sub-group into its member packages
// before installing), and for operations that don't need the live-terminal
// treatment (e.g. repository metadata refresh).
void confirmAndRun(QWidget *parentWidget, const QString &actionVerb, const QString &confirmText,
                    std::function<OperationResult()> operation,
                    std::function<void(bool success)> onFinished);

// Same shape as confirmAndRun, but computes a TransactionPreview first (in
// the background) and shows its plan text in the confirmation dialog —
// what will actually be installed/removed, including dependencies —
// instead of a static message. Falls back to `fallbackSummary` if the
// backend couldn't produce a preview.
void previewConfirmAndRun(QWidget *parentWidget, const QString &actionVerb, const QString &fallbackSummary,
                           std::function<TransactionPreview()> previewOperation,
                           std::function<OperationResult()> applyOperation,
                           std::function<void(bool success)> onFinished);

// Convenience wrappers for the common case of installing/removing/
// reinstalling a batch of packages by name, and for the dependency-cleanup
// sweep. install/remove show a dependency-resolved preview first; all four
// then run their commands in a live TerminalOutputDialog (command line +
// streaming output) rather than silently in the background.
void installPackages(QWidget *parentWidget, PackageBackend *backend, const QStringList &packageNames,
                      std::function<void(bool success)> onFinished);
void removePackages(QWidget *parentWidget, PackageBackend *backend, const QStringList &packageNames,
                     std::function<void(bool success)> onFinished);
void reinstallPackages(QWidget *parentWidget, PackageBackend *backend, const QStringList &packageNames,
                        std::function<void(bool success)> onFinished);
void cleanUnusedDependencies(QWidget *parentWidget, PackageBackend *backend,
                              std::function<void(bool success)> onFinished);

// Downloads (without installing) a batch of packages by name into
// destinationDir, optionally including their not-yet-installed
// dependencies. Same confirm-then-live-terminal treatment as the other
// wrappers above.
void downloadPackages(QWidget *parentWidget, PackageBackend *backend, const QStringList &packageNames,
                       const QString &destinationDir, bool includeDependencies,
                       std::function<void(bool success)> onFinished);

} // namespace PackageActions
