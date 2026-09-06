#include "PackageActions.h"

#include <utility>

#include <QFutureWatcher>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrent>

#include "../core/AppSettings.h"
#include "TerminalOutputDialog.h"
#include "TransactionConfirmDialog.h"

namespace {

// Keeps the confirmation dialog readable for large batches instead of
// dumping dozens of names into it.
QString summarizeNames(const QStringList &names)
{
    constexpr int maxListed = 10;
    if (names.size() <= maxListed)
        return names.join(", ");
    return names.mid(0, maxListed).join(", ")
        + QObject::tr(", and %1 more").arg(names.size() - maxListed);
}

// What a preview-then-terminal action needs computed off the UI thread
// before it can show anything: the dependency preview text and the exact
// commands to run (command computation can itself involve I/O — e.g.
// FlatpakBackend resolving which remote each app comes from).
struct PreviewAndCommands {
    TransactionPreview preview;
    QVector<ProcessRunner::Command> commands;
};

bool runTerminalDialog(QWidget *parentWidget, const QString &actionVerb,
                        const QVector<ProcessRunner::Command> &commands)
{
    TerminalOutputDialog dialog(actionVerb, parentWidget);
    return dialog.runCommandsModal(commands, AppSettings::instance().autoCloseTerminalOnSuccess());
}

} // namespace

namespace PackageActions {

void confirmAndRun(QWidget *parentWidget, const QString &actionVerb, const QString &confirmText,
                    std::function<OperationResult()> operation, std::function<void(bool)> onFinished,
                    bool requiresPrivileges)
{
    if (!TransactionConfirmDialog::confirm(parentWidget, actionVerb, confirmText, QString(), requiresPrivileges)) {
        // Callers commonly set a busy/disabled state before calling this,
        // expecting onFinished to be the one place that clears it again —
        // so it must still fire (as "not successful") on a decline, not
        // just on completion, or that state is stuck until an unrelated
        // reload happens to reset it.
        onFinished(false);
        return;
    }

    auto *watcher = new QFutureWatcher<OperationResult>();
    QFuture<OperationResult> future = QtConcurrent::run(std::move(operation));

    QObject::connect(watcher, &QFutureWatcher<OperationResult>::finished, parentWidget,
                      [watcher, parentWidget, actionVerb, onFinished]() {
                          const OperationResult result = watcher->result();
                          watcher->deleteLater();

                          if (!result.success) {
                              QMessageBox::critical(
                                  parentWidget, actionVerb,
                                  QObject::tr("%1 failed.\n\n%2")
                                      .arg(actionVerb, result.output.trimmed()));
                          }
                          onFinished(result.success);
                      });
    watcher->setFuture(future);
}

void previewConfirmAndRun(QWidget *parentWidget, const QString &actionVerb, const QString &fallbackSummary,
                           std::function<TransactionPreview()> previewOperation,
                           std::function<OperationResult()> applyOperation,
                           std::function<void(bool)> onFinished)
{
    auto *previewWatcher = new QFutureWatcher<TransactionPreview>();
    QFuture<TransactionPreview> previewFuture = QtConcurrent::run(std::move(previewOperation));

    QObject::connect(
        previewWatcher, &QFutureWatcher<TransactionPreview>::finished, parentWidget,
        [previewWatcher, parentWidget, actionVerb, fallbackSummary, applyOperation, onFinished]() {
            const TransactionPreview preview = previewWatcher->result();
            previewWatcher->deleteLater();

            const QString planText = preview.available ? preview.planText : QString();
            if (!TransactionConfirmDialog::confirm(parentWidget, actionVerb, fallbackSummary, planText)) {
                onFinished(false);
                return;
            }

            auto *applyWatcher = new QFutureWatcher<OperationResult>();
            QFuture<OperationResult> applyFuture = QtConcurrent::run(applyOperation);

            QObject::connect(applyWatcher, &QFutureWatcher<OperationResult>::finished, parentWidget,
                              [applyWatcher, parentWidget, actionVerb, onFinished]() {
                                  const OperationResult result = applyWatcher->result();
                                  applyWatcher->deleteLater();

                                  if (!result.success) {
                                      QMessageBox::critical(
                                          parentWidget, actionVerb,
                                          QObject::tr("%1 failed.\n\n%2")
                                              .arg(actionVerb, result.output.trimmed()));
                                  }
                                  onFinished(result.success);
                              });
            applyWatcher->setFuture(applyFuture);
        });
    previewWatcher->setFuture(previewFuture);
}

namespace {

// Shared by installPackages()/removePackages(): computes the dependency
// preview and the commands to run in one background pass, shows the
// preview in a confirmation dialog, then (if confirmed) runs those
// commands live in a TerminalOutputDialog.
void previewConfirmAndRunWithTerminal(QWidget *parentWidget, const QString &actionVerb,
                                       const QString &fallbackSummary,
                                       std::function<PreviewAndCommands()> computeOperation,
                                       std::function<void(bool)> onFinished)
{
    auto *watcher = new QFutureWatcher<PreviewAndCommands>();
    QFuture<PreviewAndCommands> future = QtConcurrent::run(std::move(computeOperation));

    QObject::connect(
        watcher, &QFutureWatcher<PreviewAndCommands>::finished, parentWidget,
        [watcher, parentWidget, actionVerb, fallbackSummary, onFinished]() {
            const PreviewAndCommands data = watcher->result();
            watcher->deleteLater();

            const QString planText = data.preview.available ? data.preview.planText : QString();
            if (!TransactionConfirmDialog::confirm(parentWidget, actionVerb, fallbackSummary, planText)) {
                onFinished(false);
                return;
            }

            onFinished(runTerminalDialog(parentWidget, actionVerb, data.commands));
        });
    watcher->setFuture(future);
}

// Shared by reinstallPackages()/cleanUnusedDependencies(): a plain confirm
// (no dependency preview) followed by background command computation and
// a live TerminalOutputDialog run — command computation can itself involve
// I/O (e.g. Flatpak resolving remotes, pacman listing orphans), so it
// still goes through a worker thread even without a preview to wait for.
void confirmAndRunCommandsWithTerminal(QWidget *parentWidget, const QString &actionVerb,
                                        const QString &summary,
                                        std::function<QVector<ProcessRunner::Command>()> commandsOperation,
                                        std::function<void(bool)> onFinished)
{
    if (!TransactionConfirmDialog::confirm(parentWidget, actionVerb, summary)) {
        onFinished(false);
        return;
    }

    auto *watcher = new QFutureWatcher<QVector<ProcessRunner::Command>>();
    QFuture<QVector<ProcessRunner::Command>> future = QtConcurrent::run(std::move(commandsOperation));

    QObject::connect(watcher, &QFutureWatcher<QVector<ProcessRunner::Command>>::finished, parentWidget,
                      [watcher, parentWidget, actionVerb, onFinished]() {
                          const QVector<ProcessRunner::Command> commands = watcher->result();
                          watcher->deleteLater();
                          onFinished(runTerminalDialog(parentWidget, actionVerb, commands));
                      });
    watcher->setFuture(future);
}

} // namespace

void installPackages(QWidget *parentWidget, PackageBackend *backend, const QStringList &packageNames,
                      std::function<void(bool)> onFinished)
{
    if (!backend || packageNames.isEmpty())
        return;

    const QString verb = packageNames.size() == 1 ? QObject::tr("Install") : QObject::tr("Install packages");
    const QString summary = QObject::tr("Install %1?").arg(summarizeNames(packageNames));

    previewConfirmAndRunWithTerminal(
        parentWidget, verb, summary,
        [backend, packageNames]() {
            PreviewAndCommands data;
            data.preview = backend->previewInstall(packageNames);
            data.commands = backend->installCommands(packageNames);
            return data;
        },
        std::move(onFinished));
}

void removePackages(QWidget *parentWidget, PackageBackend *backend, const QStringList &packageNames,
                     std::function<void(bool)> onFinished)
{
    if (!backend || packageNames.isEmpty())
        return;

    const QString verb = packageNames.size() == 1 ? QObject::tr("Uninstall") : QObject::tr("Uninstall packages");
    const QString summary = QObject::tr("Uninstall %1?").arg(summarizeNames(packageNames));

    previewConfirmAndRunWithTerminal(
        parentWidget, verb, summary,
        [backend, packageNames]() {
            PreviewAndCommands data;
            data.preview = backend->previewRemove(packageNames);
            data.commands = backend->removeCommands(packageNames);
            return data;
        },
        std::move(onFinished));
}

void reinstallPackages(QWidget *parentWidget, PackageBackend *backend, const QStringList &packageNames,
                        std::function<void(bool)> onFinished)
{
    if (!backend || packageNames.isEmpty())
        return;

    const QString verb = packageNames.size() == 1 ? QObject::tr("Reinstall") : QObject::tr("Reinstall packages");
    const QString summary = QObject::tr("Reinstall %1?").arg(summarizeNames(packageNames));

    confirmAndRunCommandsWithTerminal(
        parentWidget, verb, summary,
        [backend, packageNames]() { return backend->reinstallCommands(packageNames); }, std::move(onFinished));
}

void cleanUnusedDependencies(QWidget *parentWidget, PackageBackend *backend,
                              std::function<void(bool)> onFinished)
{
    if (!backend)
        return;

    const QString verb = QObject::tr("Clean Left Behind Dependencies");
    const QString summary = QObject::tr("Remove packages that were installed as dependencies but are no "
                                         "longer needed by anything installed?");

    confirmAndRunCommandsWithTerminal(
        parentWidget, verb, summary, [backend]() { return backend->cleanUnusedDependenciesCommands(); },
        std::move(onFinished));
}

void downloadPackages(QWidget *parentWidget, PackageBackend *backend, const QStringList &packageNames,
                       const QString &destinationDir, bool includeDependencies,
                       std::function<void(bool)> onFinished)
{
    if (!backend || packageNames.isEmpty())
        return;

    const QString verb = includeDependencies ? QObject::tr("Download Plus Dependencies") : QObject::tr("Download");
    const QString summary = includeDependencies
        ? QObject::tr("Download %1 plus dependencies to %2?").arg(summarizeNames(packageNames), destinationDir)
        : QObject::tr("Download %1 to %2?").arg(summarizeNames(packageNames), destinationDir);

    confirmAndRunCommandsWithTerminal(
        parentWidget, verb, summary,
        [backend, packageNames, destinationDir, includeDependencies]() {
            return backend->downloadCommands(packageNames, destinationDir, includeDependencies);
        },
        std::move(onFinished));
}

} // namespace PackageActions
