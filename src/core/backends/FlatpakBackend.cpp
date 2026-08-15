#include "FlatpakBackend.h"

#include <algorithm>

#include <QMap>
#include <QObject>
#include <QSet>

#include "../ProcessRunner.h"

using ProcessRunner::Result;

namespace {

// Both `flatpak list` and `flatpak search` accept --columns=a,b,c and emit
// one tab-separated row per entry in that column order.
QVector<QStringList> parseTabSeparatedRows(const QString &text)
{
    QVector<QStringList> rows;
    for (const QString &line : text.split('\n', Qt::SkipEmptyParts))
        rows.append(line.split('\t'));
    return rows;
}

} // namespace

QString FlatpakBackend::backendName() const
{
    return "Flatpak";
}

bool FlatpakBackend::isAvailable() const
{
    return ProcessRunner::executableExists("flatpak");
}

QVector<PackageInfo> FlatpakBackend::listInstalled()
{
    QVector<PackageInfo> packages;

    const Result result = ProcessRunner::run(
        "flatpak", {"list", "--app", "--columns=name,description,application,version,branch,arch,origin"});

    for (const QStringList &fields : parseTabSeparatedRows(result.stdOut)) {
        if (fields.size() < 7)
            continue;

        PackageInfo pkg;
        pkg.name = fields[2]; // application ID — the identifier install/remove act on
        pkg.description = fields[0] + QStringLiteral(" — ") + fields[1];
        pkg.installedVersion = !fields[3].isEmpty() ? fields[3] : fields[4];
        pkg.architecture = fields[5];
        pkg.repository = fields[6];
        pkg.installed = true;
        packages.append(pkg);
    }

    std::sort(packages.begin(), packages.end(),
              [](const PackageInfo &a, const PackageInfo &b) { return a.name < b.name; });
    return packages;
}

QVector<PackageInfo> FlatpakBackend::search(const QString &query)
{
    QVector<PackageInfo> results;
    if (query.trimmed().isEmpty())
        return results;

    const Result result = ProcessRunner::run(
        "flatpak", {"search", query, "--columns=name,description,application,version,branch,remotes"}, 30000);

    QMap<QString, PackageInfo> installedByName;
    for (const PackageInfo &pkg : listInstalled())
        installedByName.insert(pkg.name, pkg);

    for (const QStringList &fields : parseTabSeparatedRows(result.stdOut)) {
        if (fields.size() < 6)
            continue;

        PackageInfo pkg;
        pkg.name = fields[2];
        pkg.description = fields[0] + QStringLiteral(" — ") + fields[1];
        pkg.availableVersion = !fields[3].isEmpty() ? fields[3] : fields[4];
        pkg.repository = fields[5].split(',').value(0);

        const auto installedIt = installedByName.constFind(pkg.name);
        if (installedIt != installedByName.constEnd()) {
            pkg.installed = true;
            pkg.installedVersion = installedIt->installedVersion;
            pkg.architecture = installedIt->architecture;
        }

        results.append(pkg);
    }

    return results;
}

QVector<PackageGroupInfo> FlatpakBackend::listGroups()
{
    return {};
}

PackageGroupInfo FlatpakBackend::groupDetails(const QString & /*groupId*/, bool /*isMeta*/)
{
    return {};
}

QVector<PackageInfo> FlatpakBackend::packageDetails(const QStringList &packageNames)
{
    QVector<PackageInfo> results;
    if (packageNames.isEmpty())
        return results;

    QMap<QString, PackageInfo> installedByName;
    for (const PackageInfo &pkg : listInstalled())
        installedByName.insert(pkg.name, pkg);

    for (const QString &name : packageNames) {
        const auto installedIt = installedByName.constFind(name);
        if (installedIt != installedByName.constEnd()) {
            results.append(*installedIt);
            continue;
        }

        // Not installed: flatpak has no bulk "info for these exact IDs"
        // command, so fall back to a per-name search and take the exact
        // application-ID match from its results.
        const Result result = ProcessRunner::run(
            "flatpak", {"search", name, "--columns=name,description,application,version,branch,remotes"}, 15000);

        for (const QStringList &fields : parseTabSeparatedRows(result.stdOut)) {
            if (fields.size() < 6 || fields[2] != name)
                continue;

            PackageInfo pkg;
            pkg.name = fields[2];
            pkg.description = fields[0] + QStringLiteral(" — ") + fields[1];
            pkg.availableVersion = !fields[3].isEmpty() ? fields[3] : fields[4];
            pkg.repository = fields[5].split(',').value(0);
            results.append(pkg);
            break;
        }
    }

    return results;
}

QString FlatpakBackend::resolveRemoteForAppId(const QString &appId) const
{
    const Result result = ProcessRunner::run(
        "flatpak", {"search", appId, "--columns=application,remotes"}, 15000);

    for (const QStringList &fields : parseTabSeparatedRows(result.stdOut)) {
        if (fields.size() >= 2 && fields[0] == appId)
            return fields[1].split(',').value(0);
    }
    return QStringLiteral("flathub"); // reasonable default when lookup is inconclusive
}

QVector<PackageInfo> FlatpakBackend::listUpdates()
{
    QVector<PackageInfo> updates;

    const Result result = ProcessRunner::run(
        "flatpak", {"remote-ls", "--updates", "--columns=application,version,branch"}, 30000);

    QMap<QString, PackageInfo> installedByName;
    for (const PackageInfo &pkg : listInstalled())
        installedByName.insert(pkg.name, pkg);

    for (const QStringList &fields : parseTabSeparatedRows(result.stdOut)) {
        if (fields.size() < 3)
            continue;

        PackageInfo pkg;
        pkg.name = fields[0];
        pkg.availableVersion = !fields[1].isEmpty() ? fields[1] : fields[2];
        pkg.installed = true;

        const auto installedIt = installedByName.constFind(pkg.name);
        if (installedIt != installedByName.constEnd()) {
            pkg.installedVersion = installedIt->installedVersion;
            pkg.description = installedIt->description;
            pkg.repository = installedIt->repository;
        }

        updates.append(pkg);
    }
    return updates;
}

OperationResult FlatpakBackend::upgradePackages(const QStringList &packageNames)
{
    QStringList args = {"update", "-y", "--noninteractive"};
    args += packageNames;
    const Result result = ProcessRunner::run("flatpak", args, 600000);
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

OperationResult FlatpakBackend::refreshMetadata()
{
    const Result result = ProcessRunner::run("flatpak", {"update", "--appstream"}, 120000);
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

QVector<ProcessRunner::Command> FlatpakBackend::installCommands(const QStringList &packageNames) const
{
    QMap<QString, QStringList> refsByRemote;
    for (const QString &appId : packageNames)
        refsByRemote[resolveRemoteForAppId(appId)].append(appId);

    QVector<ProcessRunner::Command> commands;
    for (auto it = refsByRemote.constBegin(); it != refsByRemote.constEnd(); ++it) {
        QStringList args = {"install", "-y", "--noninteractive", it.key()};
        args += it.value();
        commands.append({"flatpak", args});
    }
    return commands;
}

QVector<ProcessRunner::Command> FlatpakBackend::removeCommands(const QStringList &packageNames) const
{
    QStringList args = {"uninstall", "-y", "--noninteractive"};
    args += packageNames;

    // Runtimes that only the just-removed app(s) needed are left behind
    // by a plain uninstall; the --unused sweep is a second command in the
    // same sequence so removal actually cleans up its own dependencies.
    return {{"flatpak", args}, {"flatpak", {"uninstall", "--unused", "-y", "--noninteractive"}}};
}

QVector<ProcessRunner::Command> FlatpakBackend::reinstallCommands(const QStringList &packageNames) const
{
    // Flatpak has no dedicated reinstall command; uninstall then install
    // the same refs. App data under ~/.var/app is untouched either way
    // (only --delete-data would remove it, which this doesn't pass).
    QVector<ProcessRunner::Command> commands = {
        {"flatpak", QStringList{"uninstall", "-y", "--noninteractive"} + packageNames}};
    commands += installCommands(packageNames);
    return commands;
}

OperationResult FlatpakBackend::installPackages(const QStringList &packageNames)
{
    const Result result = ProcessRunner::runSequence(installCommands(packageNames));
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

OperationResult FlatpakBackend::removePackages(const QStringList &packageNames)
{
    const Result result = ProcessRunner::runSequence(removeCommands(packageNames));
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

OperationResult FlatpakBackend::reinstallPackages(const QStringList &packageNames)
{
    const Result result = ProcessRunner::runSequence(reinstallCommands(packageNames));
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

TransactionPreview FlatpakBackend::previewInstall(const QStringList &packageNames)
{
    TransactionPreview preview;
    preview.available = true;

    QSet<QString> installedRefs;
    const Result listResult =
        ProcessRunner::run("flatpak", {"list", "--columns=application,arch,branch"}, 15000);
    for (const QStringList &fields : parseTabSeparatedRows(listResult.stdOut)) {
        if (fields.size() >= 3)
            installedRefs.insert(fields[0] + '/' + fields[1] + '/' + fields[2]);
    }

    QStringList installLines;
    QStringList runtimeLines;
    QSet<QString> seenRuntimes;

    for (const QString &appId : packageNames) {
        const QString remote = resolveRemoteForAppId(appId);
        installLines << QStringLiteral("  %1 (from %2)").arg(appId, remote);

        const Result infoResult = ProcessRunner::run("flatpak", {"remote-info", remote, appId}, 20000);
        for (const QString &line : infoResult.stdOut.split('\n')) {
            const QString trimmed = line.trimmed();
            if (!trimmed.startsWith(QLatin1String("Runtime:")))
                continue;
            const QString runtimeSpec = trimmed.mid(QString("Runtime:").length()).trimmed();
            if (!runtimeSpec.isEmpty() && !installedRefs.contains(runtimeSpec)
                && !seenRuntimes.contains(runtimeSpec)) {
                seenRuntimes.insert(runtimeSpec);
                runtimeLines << QStringLiteral("  %1").arg(runtimeSpec);
            }
            break;
        }
    }

    QStringList plan;
    plan << QObject::tr("Installing:") << installLines;
    if (!runtimeLines.isEmpty())
        plan << QString() << QObject::tr("Also installing runtime(s):") << runtimeLines;
    preview.planText = plan.join('\n');
    return preview;
}

TransactionPreview FlatpakBackend::previewRemove(const QStringList &packageNames)
{
    TransactionPreview preview;
    preview.available = true;

    const QSet<QString> removingSet(packageNames.begin(), packageNames.end());

    // appId -> "id/arch/branch" runtime ref, so we can tell which
    // runtimes become unused once the given apps are gone.
    QMap<QString, QString> runtimeByApp;
    const Result appsResult =
        ProcessRunner::run("flatpak", {"list", "--app", "--columns=application,runtime"}, 15000);
    for (const QStringList &fields : parseTabSeparatedRows(appsResult.stdOut)) {
        if (fields.size() >= 2)
            runtimeByApp[fields[0]] = fields[1];
    }

    QSet<QString> stillUsedRuntimes;
    for (auto it = runtimeByApp.constBegin(); it != runtimeByApp.constEnd(); ++it) {
        if (!removingSet.contains(it.key()))
            stillUsedRuntimes.insert(it.value());
    }

    QStringList removeLines;
    QStringList orphanedRuntimes;
    for (const QString &appId : packageNames) {
        removeLines << QStringLiteral("  %1").arg(appId);
        const QString runtime = runtimeByApp.value(appId);
        if (!runtime.isEmpty() && !stillUsedRuntimes.contains(runtime) && !orphanedRuntimes.contains(runtime))
            orphanedRuntimes << runtime;
    }

    QStringList plan;
    plan << QObject::tr("Removing:") << removeLines;
    if (!orphanedRuntimes.isEmpty()) {
        QStringList lines;
        for (const QString &runtime : orphanedRuntimes)
            lines << QStringLiteral("  %1").arg(runtime);
        plan << QString() << QObject::tr("Also removing (no longer used):") << lines;
    }
    preview.planText = plan.join('\n');
    return preview;
}

QVector<ProcessRunner::Command> FlatpakBackend::cleanUnusedDependenciesCommands() const
{
    return {{"flatpak", {"uninstall", "--unused", "-y", "--noninteractive"}}};
}

OperationResult FlatpakBackend::cleanUnusedDependencies()
{
    const Result result = ProcessRunner::runSequence(cleanUnusedDependenciesCommands(), 300000);
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

QString FlatpakBackend::recentHistory()
{
    const Result result =
        ProcessRunner::run("flatpak", {"history", "--reverse", "--columns=time,change,application,branch,remote"},
                            30000);
    return result.stdOut.trimmed();
}

QVector<RepositoryInfo> FlatpakBackend::listRepositories()
{
    QVector<RepositoryInfo> repos;

    const Result result =
        ProcessRunner::run("flatpak", {"remotes", "--show-disabled", "--columns=name,title,url,options"}, 15000);

    for (const QStringList &fields : parseTabSeparatedRows(result.stdOut)) {
        if (fields.size() < 4)
            continue;

        RepositoryInfo repo;
        repo.id = fields[0];
        repo.name = (!fields[1].isEmpty() && fields[1] != QLatin1String("-")) ? fields[1] : fields[0];
        repo.url = fields[2];
        repo.enabled = !fields[3].split(',').contains(QLatin1String("disabled"));
        repos.append(repo);
    }
    return repos;
}

OperationResult FlatpakBackend::setRepositoryEnabled(const QString &repoId, bool enabled)
{
    // No pkexec wrapping needed: like install/remove, flatpak's own polkit
    // action covers system-wide remote modification too.
    const Result result =
        ProcessRunner::run("flatpak", {"remote-modify", enabled ? "--enable" : "--disable", repoId}, 30000);
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

QVector<RepositoryAddField> FlatpakBackend::repositoryAddFields() const
{
    return {
        {"name", "Remote Name", "myremote", true},
        {"url", "Remote URL", "https://example.com/repo/repo.flatpakrepo", true},
    };
}

OperationResult FlatpakBackend::addRepository(const RepositoryAddValues &values)
{
    const QString name = values.value("name").trimmed();
    const QString url = values.value("url").trimmed();

    OperationResult op;
    if (name.isEmpty() || url.isEmpty()) {
        op.output = QStringLiteral("Remote name and URL are required");
        return op;
    }

    const Result result =
        ProcessRunner::run("flatpak", {"remote-add", "--if-not-exists", name, url}, 30000);
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}
