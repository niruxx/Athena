#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// Thin wrapper around QProcess for running package-manager CLI commands
// synchronously and capturing their output. Kept separate from the
// backends so each backend only deals with parsing, not process plumbing.
namespace ProcessRunner {

struct Result {
    int exitCode = -1;
    QString stdOut;
    QString stdErr;
    bool started = false;
};

// A single program + argument list, with nothing backend-specific about
// it — used both for immediate execution here and, unexecuted, as a
// description of "what would run" (e.g. for TerminalOutputDialog to run
// live and show the user, or for a preview to describe).
struct Command {
    QString program;
    QStringList args;
    // Empty means "current directory" (the default for every existing
    // caller). Set for commands that only know how to write output
    // relative to their cwd (e.g. `apt-get download`, `snap download`)
    // rather than accepting an explicit destination flag.
    QString workingDirectory;
};

// Runs `program` with `args` and waits for it to finish (bounded by
// timeoutMs). Does not go through a shell, so no quoting concerns.
Result run(const QString &program, const QStringList &args, int timeoutMs = 30000,
           const QString &workingDirectory = QString());

// Same, but writes `stdinData` to the child's stdin before waiting for it
// to finish. Used for e.g. `pkexec tee <path>` to write a file as root
// without a shell (and its redirection/quoting concerns) in between.
Result runWithStdin(const QString &program, const QStringList &args, const QByteArray &stdinData,
                     int timeoutMs = 30000);

// Runs each command in order, stopping at the first failure (non-zero
// exit or failure to start). The combined Result's stdOut/stdErr are the
// concatenation of every step that ran; exitCode/started reflect the step
// that stopped the sequence (or the last one, if all succeeded).
Result runSequence(const QVector<Command> &commands, int timeoutMsPerCommand = 600000);

// Returns true if `program` resolves to something executable on PATH.
bool executableExists(const QString &program);

} // namespace ProcessRunner
