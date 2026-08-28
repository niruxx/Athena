#include "SnapBackend.h"

#include <algorithm>

#include <QMap>
#include <QObject>
#include <QRegularExpression>
#include <QSet>

#include "../ProcessRunner.h"

using ProcessRunner::Result;

namespace {

// `snap list`/`snap list --all`/`snap refresh --list` all emit a header
// row followed by whitespace-separated columns with no embedded spaces in
// any field, so a plain split on whitespace runs is enough (unlike
// `snap find`, whose last column is a free-text summary).
QStringList splitColumns(const QString &line)
{
    return line.trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

QStringList dataLines(const QString &text)
{
    QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    if (!lines.isEmpty() && lines.first().startsWith(QLatin1String("Name")))
        lines.removeFirst();
    return lines;
}

// Parses one `snap info <name>` block: top-level "key: value" lines (no
// leading whitespace), with the multi-line "description: |" block
// collected separately since it's the only field this backend needs that
// can span lines.
struct SnapInfoBlock {
    QMap<QString, QString> fields;
    QString description;
};

SnapInfoBlock parseSnapInfo(const QString &text)
{
    SnapInfoBlock block;
    static const QRegularExpression keyPattern(QStringLiteral(R"(^(\S[\w-]*):\s?(.*)$)"));

    bool inDescription = false;
    QStringList descriptionLines;

    for (const QString &rawLine : text.split('\n')) {
        const bool indented = rawLine.startsWith(' ') || rawLine.startsWith('\t');

        if (indented) {
            if (inDescription)
                descriptionLines << rawLine.trimmed();
            continue;
        }

        inDescription = false;
        const QRegularExpressionMatch match = keyPattern.match(rawLine);
        if (!match.hasMatch())
            continue;

        const QString key = match.captured(1);
        const QString value = match.captured(2).trimmed();
        block.fields[key] = value;

        if (key == QLatin1String("description")) {
            if (value.isEmpty() || value == QLatin1String("|")) {
                inDescription = true;
            } else {
                descriptionLines << value;
            }
        }
    }
    block.description = descriptionLines.join('\n');
    return block;
}

} // namespace

QString SnapBackend::backendName() const
{
    return "Snap";
}

bool SnapBackend::isAvailable() const
{
    return ProcessRunner::executableExists("snap");
}

QVector<PackageInfo> SnapBackend::listInstalled()
{
    QVector<PackageInfo> packages;

    const Result result = ProcessRunner::run("snap", {"list"});
    for (const QString &line : dataLines(result.stdOut)) {
        const QStringList fields = splitColumns(line);
        if (fields.size() < 4)
            continue;

        PackageInfo pkg;
        pkg.name = fields[0];
        pkg.installedVersion = fields[1];
        pkg.repository = fields[3]; // tracking channel, e.g. "latest/stable"
        pkg.installed = true;
        packages.append(pkg);
    }

    std::sort(packages.begin(), packages.end(),
              [](const PackageInfo &a, const PackageInfo &b) { return a.name < b.name; });
    return packages;
}

QVector<PackageInfo> SnapBackend::search(const QString &query)
{
    QVector<PackageInfo> results;
    if (query.trimmed().isEmpty())
        return results;

    const Result result = ProcessRunner::run("snap", {"find", query}, 30000);

    QMap<QString, PackageInfo> installedByName;
    for (const PackageInfo &pkg : listInstalled())
        installedByName.insert(pkg.name, pkg);

    // Name, Version, Publisher, Notes, then a free-text Summary that may
    // itself contain spaces — captured as everything after the 4th column.
    static const QRegularExpression rowPattern(
        QStringLiteral(R"(^(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(.*)$)"));

    for (const QString &line : dataLines(result.stdOut)) {
        const QRegularExpressionMatch match = rowPattern.match(line);
        if (!match.hasMatch())
            continue;

        PackageInfo pkg;
        pkg.name = match.captured(1);
        pkg.availableVersion = match.captured(2);
        pkg.repository = match.captured(3); // publisher
        pkg.description = match.captured(5);

        const auto installedIt = installedByName.constFind(pkg.name);
        if (installedIt != installedByName.constEnd()) {
            pkg.installed = true;
            pkg.installedVersion = installedIt->installedVersion;
        }

        results.append(pkg);
    }
    return results;
}

QVector<PackageInfo> SnapBackend::dependencyQuery(const QString & /*capability*/, bool /*findRequires*/)
{
    // Snaps bundle their own dependencies inside the snap image; there's
    // no package-level capability graph to query the way rpm/dpkg have.
    return {};
}

QVector<PackageGroupInfo> SnapBackend::listGroups()
{
    return {};
}

PackageGroupInfo SnapBackend::groupDetails(const QString & /*groupId*/, bool /*isMeta*/)
{
    return {};
}

QVector<PackageInfo> SnapBackend::packageDetails(const QStringList &packageNames)
{
    QVector<PackageInfo> results;
    if (packageNames.isEmpty())
        return results;

    for (const QString &name : packageNames) {
        // One call per name: `snap info` accepts multiple names but its
        // multi-snap output format isn't reliably documented, while the
        // single-snap block format below is stable.
        const Result result = ProcessRunner::run("snap", {"info", name}, 20000);
        if (!result.started || result.exitCode != 0)
            continue;

        const SnapInfoBlock block = parseSnapInfo(result.stdOut);
        if (block.fields.value("name").isEmpty())
            continue;

        PackageInfo pkg;
        pkg.name = block.fields.value("name");
        pkg.description = block.fields.value("summary");
        pkg.longDescription = !block.description.isEmpty() ? block.description : pkg.description;
        pkg.repository = block.fields.value("publisher");

        const QString installedLine = block.fields.value("installed");
        if (!installedLine.isEmpty()) {
            pkg.installed = true;
            pkg.installedVersion = installedLine.section(' ', 0, 0);
        }
        const QString trackingLine = block.fields.value("tracking");
        if (!trackingLine.isEmpty() && pkg.repository.isEmpty())
            pkg.repository = trackingLine;

        results.append(pkg);
    }
    return results;
}

QVector<PackageInfo> SnapBackend::listUpdates()
{
    QVector<PackageInfo> updates;

    const Result result = ProcessRunner::run("snap", {"refresh", "--list"}, 30000);
    if (result.stdOut.contains(QLatin1String("up to date"), Qt::CaseInsensitive))
        return updates;

    QMap<QString, PackageInfo> installedByName;
    for (const PackageInfo &pkg : listInstalled())
        installedByName.insert(pkg.name, pkg);

    for (const QString &line : dataLines(result.stdOut)) {
        const QStringList fields = splitColumns(line);
        if (fields.size() < 2)
            continue;

        PackageInfo pkg;
        pkg.name = fields[0];
        pkg.availableVersion = fields[1];
        pkg.installed = true;

        const auto installedIt = installedByName.constFind(pkg.name);
        if (installedIt != installedByName.constEnd()) {
            pkg.installedVersion = installedIt->installedVersion;
            pkg.repository = installedIt->repository;
        }
        updates.append(pkg);
    }
    return updates;
}

OperationResult SnapBackend::upgradePackages(const QStringList &packageNames)
{
    QStringList args = {"refresh"};
    args += packageNames;
    const Result result = ProcessRunner::run("pkexec", QStringList{"snap"} + args, 600000);
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

OperationResult SnapBackend::refreshMetadata()
{
    // Snap has no local package-index cache to refresh — `snap find`/`snap
    // refresh --list` always query the store live, so there's nothing to
    // do here (kept as a clean success rather than a no-op that looks like
    // a failure).
    OperationResult op;
    op.success = true;
    op.output = QObject::tr("Snap has no separate metadata cache — package information is always "
                             "fetched live from the store.");
    return op;
}

QVector<ProcessRunner::Command> SnapBackend::installCommands(const QStringList &packageNames) const
{
    QStringList args = {"snap", "install"};
    args += packageNames;
    return {{"pkexec", args}};
}

QVector<ProcessRunner::Command> SnapBackend::removeCommands(const QStringList &packageNames) const
{
    QStringList args = {"snap", "remove"};
    args += packageNames;
    return {{"pkexec", args}};
}

QVector<ProcessRunner::Command> SnapBackend::reinstallCommands(const QStringList &packageNames) const
{
    // No dedicated reinstall verb: remove then install, same as Flatpak.
    QVector<ProcessRunner::Command> commands = {
        {"pkexec", QStringList{"snap", "remove"} + packageNames}};
    commands += installCommands(packageNames);
    return commands;
}

OperationResult SnapBackend::installPackages(const QStringList &packageNames)
{
    const Result result = ProcessRunner::runSequence(installCommands(packageNames));
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

OperationResult SnapBackend::removePackages(const QStringList &packageNames)
{
    const Result result = ProcessRunner::runSequence(removeCommands(packageNames));
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

OperationResult SnapBackend::reinstallPackages(const QStringList &packageNames)
{
    const Result result = ProcessRunner::runSequence(reinstallCommands(packageNames));
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

TransactionPreview SnapBackend::previewInstall(const QStringList & /*packageNames*/)
{
    // Snaps are self-contained (no shared-dependency tree to reveal the
    // way dnf/apt/pacman have), and the snap CLI has no dry-run flag — the
    // confirmation dialog falls back to its plain summary instead.
    return {};
}

TransactionPreview SnapBackend::previewRemove(const QStringList & /*packageNames*/)
{
    return {};
}

QVector<ProcessRunner::Command> SnapBackend::cleanUnusedDependenciesCommands() const
{
    // Snap keeps old (disabled) revisions of each installed snap around for
    // rollback by default; that's the closest thing this backend has to
    // "left behind" cruft. `snap list --all` (unprivileged) reveals them.
    const Result all = ProcessRunner::run("snap", {"list", "--all"}, 30000);

    QVector<ProcessRunner::Command> commands;
    for (const QString &line : dataLines(all.stdOut)) {
        const QStringList fields = splitColumns(line);
        if (fields.size() < 6 || !fields.last().contains(QLatin1String("disabled")))
            continue;

        const QString &name = fields[0];
        const QString &revision = fields[2];
        commands.append({"pkexec", {"snap", "remove", name, QStringLiteral("--revision=%1").arg(revision)}});
    }
    return commands;
}

OperationResult SnapBackend::cleanUnusedDependencies()
{
    const QVector<ProcessRunner::Command> commands = cleanUnusedDependenciesCommands();
    OperationResult op;
    if (commands.isEmpty()) {
        op.success = true;
        op.output = QStringLiteral("No disabled snap revisions to remove.");
        return op;
    }

    const Result result = ProcessRunner::runSequence(commands, 300000);
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

QString SnapBackend::recentHistory()
{
    const Result result = ProcessRunner::run("snap", {"changes"}, 30000);
    QStringList lines = result.stdOut.split('\n', Qt::SkipEmptyParts);
    if (lines.isEmpty())
        return result.stdOut.trimmed();

    const QString header = lines.takeFirst();
    std::reverse(lines.begin(), lines.end());
    lines.prepend(header);
    return lines.join('\n');
}

QVector<RepositoryInfo> SnapBackend::listRepositories()
{
    return {};
}

OperationResult SnapBackend::setRepositoryEnabled(const QString & /*repoId*/, bool /*enabled*/)
{
    OperationResult op;
    op.output = QStringLiteral("Snap has no per-repository concept to toggle (one global store).");
    return op;
}

QVector<ProcessRunner::Command> SnapBackend::downloadCommands(const QStringList &packageNames,
                                                               const QString &destinationDir,
                                                               bool includeDependencies) const
{
    QStringList names = packageNames;

    if (includeDependencies) {
        // A snap's only real "dependency" other than itself is its base
        // snap (e.g. core22), declared in `snap info`'s "base:" field.
        QSet<QString> queuedBases;
        for (const QString &name : packageNames) {
            const Result infoResult = ProcessRunner::run("snap", {"info", name}, 20000);
            if (!infoResult.started || infoResult.exitCode != 0)
                continue;
            const SnapInfoBlock block = parseSnapInfo(infoResult.stdOut);
            const QString base = block.fields.value("base");
            if (!base.isEmpty() && !queuedBases.contains(base) && !names.contains(base)) {
                queuedBases.insert(base);
                names << base;
            }
        }
    }

    QStringList args = {"download"};
    args += names;
    // snap download always writes into its current directory (it has no
    // destination flag), so destinationDir is carried as the command's
    // working directory instead. Unprivileged — no pkexec needed.
    ProcessRunner::Command command;
    command.program = "snap";
    command.args = args;
    command.workingDirectory = destinationDir;
    return {command};
}

OperationResult SnapBackend::downloadPackages(const QStringList &packageNames, const QString &destinationDir,
                                               bool includeDependencies)
{
    const Result result =
        ProcessRunner::runSequence(downloadCommands(packageNames, destinationDir, includeDependencies));
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

QVector<RepositoryAddField> SnapBackend::repositoryAddFields() const
{
    return {};
}

OperationResult SnapBackend::addRepository(const RepositoryAddValues & /*values*/)
{
    OperationResult op;
    op.output = QStringLiteral("Snap has no concept of additional repositories (one global store).");
    return op;
}
