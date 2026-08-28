#include "ProcessRunner.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace ProcessRunner {

namespace {

Result runInternal(const QString &program, const QStringList &args, const QByteArray *stdinData,
                    int timeoutMs, const QString &workingDirectory)
{
    Result result;

    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Force stable, unlocalized output so parsing doesn't break on
    // translated CLI output.
    env.insert("LC_ALL", "C");
    process.setProcessEnvironment(env);
    if (!workingDirectory.isEmpty())
        process.setWorkingDirectory(workingDirectory);

    process.start(program, args);
    if (!process.waitForStarted(5000)) {
        result.started = false;
        return result;
    }
    result.started = true;

    if (stdinData) {
        process.write(*stdinData);
        process.closeWriteChannel();
    }

    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
    }

    result.exitCode = process.exitCode();
    result.stdOut = QString::fromUtf8(process.readAllStandardOutput());
    result.stdErr = QString::fromUtf8(process.readAllStandardError());
    return result;
}

} // namespace

Result run(const QString &program, const QStringList &args, int timeoutMs, const QString &workingDirectory)
{
    return runInternal(program, args, nullptr, timeoutMs, workingDirectory);
}

Result runWithStdin(const QString &program, const QStringList &args, const QByteArray &stdinData,
                     int timeoutMs)
{
    return runInternal(program, args, &stdinData, timeoutMs, QString());
}

Result runSequence(const QVector<Command> &commands, int timeoutMsPerCommand)
{
    Result combined;
    combined.started = true;
    combined.exitCode = 0;

    for (const Command &command : commands) {
        const Result stepResult =
            run(command.program, command.args, timeoutMsPerCommand, command.workingDirectory);
        combined.stdOut += stepResult.stdOut;
        combined.stdErr += stepResult.stdErr;

        if (!stepResult.started || stepResult.exitCode != 0) {
            combined.started = stepResult.started;
            combined.exitCode = stepResult.exitCode;
            return combined;
        }
    }
    return combined;
}

bool executableExists(const QString &program)
{
    return !QStandardPaths::findExecutable(program).isEmpty();
}

} // namespace ProcessRunner
