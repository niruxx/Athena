#include "PacmanBackend.h"

#include <algorithm>

#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QTextStream>

#include "../ProcessRunner.h"

using ProcessRunner::Result;

QString PacmanBackend::backendName() const
{
    return "Pacman (Arch Linux)";
}

bool PacmanBackend::isAvailable() const
{
    return ProcessRunner::executableExists("pacman");
}

QVector<PackageInfo> PacmanBackend::listInstalled()
{
    QVector<PackageInfo> packages;

    const Result result = ProcessRunner::run("pacman", {"-Q"});
    const QStringList lines = result.stdOut.split('\n', Qt::SkipEmptyParts);
    packages.reserve(lines.size());
    for (const QString &line : lines) {
        const QStringList fields = line.split(' ', Qt::SkipEmptyParts);
        if (fields.size() < 2)
            continue;
        PackageInfo pkg;
        pkg.name = fields[0];
        pkg.installedVersion = fields[1];
        pkg.repository = "installed";
        pkg.installed = true;
        packages.append(pkg);
    }

    std::sort(packages.begin(), packages.end(),
              [](const PackageInfo &a, const PackageInfo &b) { return a.name < b.name; });
    return packages;
}

QVector<PackageInfo> PacmanBackend::search(const QString &query)
{
    QVector<PackageInfo> results;

    const Result result = ProcessRunner::run("pacman", {"-Ss", query}, 60000);
    const QStringList lines = result.stdOut.split('\n');

    static const QRegularExpression headerPattern(
        QStringLiteral(R"(^(\S+)/(\S+)\s+(\S+)(?:\s+\(([^)]*)\))?(\s+\[installed[^\]]*\])?$)"));

    PackageInfo pending;
    bool havePending = false;
    for (const QString &line : lines) {
        if (!line.isEmpty() && (line[0] == ' ' || line[0] == '\t')) {
            if (havePending)
                pending.description = line.trimmed();
            continue;
        }

        if (havePending)
            results.append(pending);
        havePending = false;

        const QRegularExpressionMatch match = headerPattern.match(line);
        if (!match.hasMatch())
            continue;

        pending = PackageInfo();
        pending.repository = match.captured(1);
        pending.name = match.captured(2);
        pending.availableVersion = match.captured(3);
        pending.installedVersion = match.capturedLength(5) > 0 ? match.captured(3) : QString();
        pending.installed = match.capturedLength(5) > 0;
        havePending = true;
    }
    if (havePending)
        results.append(pending);

    return results;
}

namespace {

// Parses `pacman -Qi`/`-Si` output: one blank-line-separated "Key : Value"
// block per package (pacman doesn't wrap long values across lines the way
// dnf5 does, so no continuation handling is needed).
QMap<QString, QMap<QString, QString>> parsePacmanInfoBlocks(const QString &text)
{
    QMap<QString, QMap<QString, QString>> blocks;
    QString currentName;
    QMap<QString, QString> currentFields;

    auto flush = [&]() {
        if (!currentName.isEmpty())
            blocks[currentName] = currentFields;
        currentName.clear();
        currentFields.clear();
    };

    for (const QString &rawLine : text.split('\n')) {
        if (rawLine.trimmed().isEmpty()) {
            flush();
            continue;
        }
        const int colonIndex = rawLine.indexOf(':');
        if (colonIndex < 0)
            continue;
        const QString key = rawLine.left(colonIndex).trimmed();
        const QString value = rawLine.mid(colonIndex + 1).trimmed();
        if (key == QLatin1String("Name"))
            currentName = value;
        currentFields[key] = value;
    }
    flush();

    return blocks;
}

} // namespace

QVector<PackageInfo> PacmanBackend::packageDetails(const QStringList &packageNames)
{
    if (packageNames.isEmpty())
        return {};

    QStringList localArgs = {"-Qi"};
    localArgs += packageNames;
    const Result localResult = ProcessRunner::run("pacman", localArgs, 30000);
    const auto localBlocks = parsePacmanInfoBlocks(localResult.stdOut);

    QStringList syncArgs = {"-Si"};
    syncArgs += packageNames;
    const Result syncResult = ProcessRunner::run("pacman", syncArgs, 30000);
    const auto syncBlocks = parsePacmanInfoBlocks(syncResult.stdOut);

    QVector<PackageInfo> results;
    for (const QString &name : packageNames) {
        if (!localBlocks.contains(name) && !syncBlocks.contains(name))
            continue;

        PackageInfo pkg;
        pkg.name = name;

        if (localBlocks.contains(name)) {
            const QMap<QString, QString> &fields = localBlocks[name];
            pkg.installedVersion = fields.value("Version");
            pkg.installed = true;
            pkg.architecture = fields.value("Architecture");
            pkg.description = fields.value("Description");
            pkg.longDescription = fields.value("Description");
            pkg.repository = "installed";
            const QString url = fields.value("URL");
            if (url != QLatin1String("None"))
                pkg.homepageUrl = url;
            pkg.size = fields.value("Installed Size");
        }
        if (syncBlocks.contains(name)) {
            const QMap<QString, QString> &fields = syncBlocks[name];
            pkg.availableVersion = fields.value("Version");
            if (pkg.architecture.isEmpty())
                pkg.architecture = fields.value("Architecture");
            if (pkg.description.isEmpty()) {
                pkg.description = fields.value("Description");
                pkg.longDescription = fields.value("Description");
            }
            if (!pkg.installed)
                pkg.repository = fields.value("Repository");
            if (pkg.homepageUrl.isEmpty()) {
                const QString url = fields.value("URL");
                if (url != QLatin1String("None"))
                    pkg.homepageUrl = url;
            }
            if (pkg.size.isEmpty())
                pkg.size = fields.value("Download Size");
        }

        results.append(pkg);
    }
    return results;
}

QVector<PackageInfo> PacmanBackend::dependencyQuery(const QString &capability, bool findRequires)
{
    const QString trimmed = capability.trimmed();
    if (trimmed.isEmpty())
        return {};

    if (!findRequires) {
        // Pacman has no non-interactive "what provides this capability
        // across the whole sync database" query (resolving a provider via
        // `pacman -S` can prompt interactively when more than one package
        // provides it), so this direction isn't supported here.
        return {};
    }

    // "Required By" is only meaningful for an installed package — pacman
    // has no reverse-dependency query against the sync database for
    // packages that aren't installed.
    const Result result = ProcessRunner::run("pacman", {"-Qi", trimmed}, 15000);
    const auto blocks = parsePacmanInfoBlocks(result.stdOut);
    if (!blocks.contains(trimmed))
        return {};

    const QString requiredBy = blocks[trimmed].value("Required By").trimmed();
    if (requiredBy.isEmpty() || requiredBy.compare(QLatin1String("None"), Qt::CaseInsensitive) == 0)
        return {};

    return packageDetails(requiredBy.split(' ', Qt::SkipEmptyParts));
}

QVector<PackageGroupInfo> PacmanBackend::listGroups()
{
    QVector<PackageGroupInfo> groups;

    const Result allGroups = ProcessRunner::run("pacman", {"-Sg"}, 30000);
    QMap<QString, QStringList> groupPackages;
    QStringList order;
    for (const QString &line : allGroups.stdOut.split('\n', Qt::SkipEmptyParts)) {
        const QStringList fields = line.split(' ', Qt::SkipEmptyParts);
        if (fields.size() < 2)
            continue;
        const QString &groupName = fields[0];
        if (!groupPackages.contains(groupName))
            order.append(groupName);
        groupPackages[groupName].append(fields[1]);
    }

    for (const QString &groupName : order) {
        PackageGroupInfo group;
        group.id = groupName;
        group.name = groupName;
        group.isMeta = false;
        group.packages = groupPackages.value(groupName);

        const Result installedCheck = ProcessRunner::run("pacman", {"-Qg", groupName}, 15000);
        group.installed = installedCheck.exitCode == 0 && !installedCheck.stdOut.trimmed().isEmpty();

        groups.append(group);
    }

    return groups;
}

PackageGroupInfo PacmanBackend::groupDetails(const QString &groupId, bool /*isMeta*/)
{
    PackageGroupInfo group;
    group.id = groupId;
    group.name = groupId;
    group.isMeta = false;

    const Result result = ProcessRunner::run("pacman", {"-Sg", groupId}, 30000);
    for (const QString &line : result.stdOut.split('\n', Qt::SkipEmptyParts)) {
        const QStringList fields = line.split(' ', Qt::SkipEmptyParts);
        if (fields.size() < 2)
            continue;
        group.packages.append(fields[1]);
    }

    const Result installedCheck = ProcessRunner::run("pacman", {"-Qg", groupId}, 15000);
    group.installed = installedCheck.exitCode == 0 && !installedCheck.stdOut.trimmed().isEmpty();

    return group;
}

QVector<PackageInfo> PacmanBackend::listUpdates()
{
    QVector<PackageInfo> updates;

    const Result result = ProcessRunner::run("pacman", {"-Qu"}, 30000);
    static const QRegularExpression pattern(QStringLiteral(R"(^(\S+)\s+(\S+)\s+->\s+(\S+))"));

    for (const QString &line : result.stdOut.split('\n', Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch match = pattern.match(line);
        if (!match.hasMatch())
            continue;

        PackageInfo pkg;
        pkg.name = match.captured(1);
        pkg.installedVersion = match.captured(2);
        pkg.availableVersion = match.captured(3);
        pkg.installed = true;
        pkg.repository = QStringLiteral("installed");
        updates.append(pkg);
    }
    return updates;
}

OperationResult PacmanBackend::upgradePackages(const QStringList &packageNames)
{
    QStringList args = {"pacman", "-S", "--noconfirm"};
    args += packageNames;
    const Result result = ProcessRunner::run("pkexec", args, 600000);
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

OperationResult PacmanBackend::refreshMetadata()
{
    const Result result = ProcessRunner::run("pkexec", {"pacman", "-Sy", "--noconfirm"}, 120000);
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

QVector<ProcessRunner::Command> PacmanBackend::installCommands(const QStringList &packageNames) const
{
    QStringList args = {"pacman", "-S", "--noconfirm"};
    args += packageNames;
    return {{"pkexec", args}};
}

QVector<ProcessRunner::Command> PacmanBackend::removeCommands(const QStringList &packageNames) const
{
    // -Rs (not plain -R): also removes dependencies that were pulled in
    // for these packages and aren't required by anything else installed.
    QStringList args = {"pacman", "-Rs", "--noconfirm"};
    args += packageNames;
    return {{"pkexec", args}};
}

QVector<ProcessRunner::Command> PacmanBackend::reinstallCommands(const QStringList &packageNames) const
{
    // Plain -S reinstalls an explicitly-named already-installed package by
    // default (only --needed would skip it), so no special flag is needed.
    QStringList args = {"pacman", "-S", "--noconfirm"};
    args += packageNames;
    return {{"pkexec", args}};
}

OperationResult PacmanBackend::installPackages(const QStringList &packageNames)
{
    const Result result = ProcessRunner::runSequence(installCommands(packageNames));
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

OperationResult PacmanBackend::removePackages(const QStringList &packageNames)
{
    const Result result = ProcessRunner::runSequence(removeCommands(packageNames));
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

OperationResult PacmanBackend::reinstallPackages(const QStringList &packageNames)
{
    const Result result = ProcessRunner::runSequence(reinstallCommands(packageNames));
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

TransactionPreview PacmanBackend::previewInstall(const QStringList &packageNames)
{
    QStringList args = {"-S", "--print", "--print-format", "%n %v"};
    args += packageNames;
    const Result result = ProcessRunner::run("pacman", args, 60000);

    TransactionPreview preview;
    preview.available = result.started;
    QStringList lines;
    for (const QString &line : result.stdOut.split('\n', Qt::SkipEmptyParts))
        lines << QStringLiteral("  %1").arg(line.trimmed());
    if (!lines.isEmpty())
        preview.planText = QObject::tr("Installing (including dependencies):\n%1").arg(lines.join('\n'));
    return preview;
}

TransactionPreview PacmanBackend::previewRemove(const QStringList &packageNames)
{
    QStringList args = {"-Rs", "--print", "--print-format", "%n"};
    args += packageNames;
    const Result result = ProcessRunner::run("pacman", args, 60000);

    TransactionPreview preview;
    preview.available = result.started;
    QStringList lines;
    for (const QString &line : result.stdOut.split('\n', Qt::SkipEmptyParts))
        lines << QStringLiteral("  %1").arg(line.trimmed());
    if (!lines.isEmpty())
        preview.planText =
            QObject::tr("Removing (including now-unused dependencies):\n%1").arg(lines.join('\n'));
    return preview;
}

QVector<ProcessRunner::Command> PacmanBackend::cleanUnusedDependenciesCommands() const
{
    // pacman has no single "autoremove"; the documented equivalent is to
    // list orphaned dependencies (-Qdtq, unprivileged) and remove them.
    const Result orphans = ProcessRunner::run("pacman", {"-Qdtq"}, 30000);
    const QStringList names = orphans.stdOut.split('\n', Qt::SkipEmptyParts);
    if (names.isEmpty())
        return {};

    QStringList args = {"pacman", "-R", "--noconfirm"};
    args += names;
    return {{"pkexec", args}};
}

OperationResult PacmanBackend::cleanUnusedDependencies()
{
    const QVector<ProcessRunner::Command> commands = cleanUnusedDependenciesCommands();
    OperationResult op;
    if (commands.isEmpty()) {
        op.success = true;
        op.output = QStringLiteral("No unused dependencies to remove.");
        return op;
    }

    const Result result = ProcessRunner::runSequence(commands, 300000);
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

namespace {

const QString kPacmanConfPath = QStringLiteral("/etc/pacman.conf");

// Directives that can legitimately appear (commented or not) inside a
// repo section; only these plus the section header itself are ever
// toggled, so unrelated comments a user left in the file are untouched.
bool isToggleableDirective(const QString &trimmedLine)
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^#?\s*(Include|Server|SigLevel|Usage)\b)"));
    return pattern.match(trimmedLine).hasMatch();
}

QStringList readLines(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream stream(&file);
    QStringList lines;
    while (!stream.atEnd())
        lines << stream.readLine();
    return lines;
}

bool writeFileAsRoot(const QString &path, const QStringList &lines)
{
    const QByteArray content = (lines.join('\n') + '\n').toUtf8();
    const Result result = ProcessRunner::runWithStdin("pkexec", {"tee", path}, content, 30000);
    return result.started && result.exitCode == 0;
}

} // namespace

QString PacmanBackend::recentHistory()
{
    QFile file(QStringLiteral("/var/log/pacman.log"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("No history log found (expected at /var/log/pacman.log).");

    QTextStream stream(&file);
    QStringList lines;
    while (!stream.atEnd())
        lines << stream.readLine();

    std::reverse(lines.begin(), lines.end());
    constexpr int maxLines = 500;
    if (lines.size() > maxLines)
        lines = lines.mid(0, maxLines);

    return lines.join('\n');
}

QVector<RepositoryInfo> PacmanBackend::listRepositories()
{
    QVector<RepositoryInfo> repos;
    const QStringList lines = readLines(kPacmanConfPath);

    static const QRegularExpression headerPattern(QStringLiteral(R"(^(#)?\s*\[([^\]]+)\]\s*$)"));
    static const QRegularExpression urlPattern(
        QStringLiteral(R"(^#?\s*(?:Include|Server)\s*=\s*(.*)$)"));

    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines[i].trimmed();
        const QRegularExpressionMatch headerMatch = headerPattern.match(trimmed);
        if (!headerMatch.hasMatch())
            continue;

        const QString sectionName = headerMatch.captured(2);
        if (sectionName.compare("options", Qt::CaseInsensitive) == 0)
            continue; // not a repo

        RepositoryInfo repo;
        repo.id = QString::number(i);
        repo.name = sectionName;
        repo.enabled = headerMatch.capturedLength(1) == 0;

        for (int j = i + 1; j < lines.size(); ++j) {
            const QString bodyTrimmed = lines[j].trimmed();
            if (bodyTrimmed.startsWith('['))
                break; // next section
            const QRegularExpressionMatch urlMatch = urlPattern.match(bodyTrimmed);
            if (urlMatch.hasMatch()) {
                repo.url = urlMatch.captured(1).trimmed();
                break;
            }
        }

        repos.append(repo);
    }
    return repos;
}

OperationResult PacmanBackend::setRepositoryEnabled(const QString &repoId, bool enabled)
{
    QStringList lines = readLines(kPacmanConfPath);
    OperationResult op;

    bool ok = false;
    const int headerIndex = repoId.toInt(&ok);
    if (!ok || headerIndex < 0 || headerIndex >= lines.size()) {
        op.output = QStringLiteral("Repository entry no longer found in %1").arg(kPacmanConfPath);
        return op;
    }

    int sectionEnd = headerIndex + 1;
    while (sectionEnd < lines.size() && !lines[sectionEnd].trimmed().startsWith('['))
        ++sectionEnd;

    for (int i = headerIndex; i < sectionEnd; ++i) {
        const QString trimmed = lines[i].trimmed();
        const bool isHeader = (i == headerIndex);
        if (!isHeader && !isToggleableDirective(trimmed))
            continue;

        const bool currentlyCommented = trimmed.startsWith('#');
        if (enabled && currentlyCommented) {
            int hashPos = lines[i].indexOf('#');
            lines[i].remove(hashPos, 1);
            if (lines[i].mid(hashPos, 1) == QStringLiteral(" "))
                lines[i].remove(hashPos, 1);
        } else if (!enabled && !currentlyCommented) {
            const int firstNonSpace = lines[i].indexOf(QRegularExpression(QStringLiteral(R"(\S)")));
            lines[i].insert(firstNonSpace < 0 ? 0 : firstNonSpace, QStringLiteral("#"));
        }
    }

    op.success = writeFileAsRoot(kPacmanConfPath, lines);
    if (!op.success)
        op.output = QStringLiteral("Failed to write %1 (are you authorized for admin actions?)").arg(kPacmanConfPath);
    return op;
}

QVector<RepositoryAddField> PacmanBackend::repositoryAddFields() const
{
    return {
        {"name", "Repository Name", "myrepo", true},
        {"server", "Server URL", "https://example.com/repo/$arch", true},
    };
}

QVector<ProcessRunner::Command> PacmanBackend::downloadCommands(const QStringList &packageNames,
                                                                 const QString &destinationDir,
                                                                 bool includeDependencies) const
{
    // -Sw downloads without installing; it resolves and downloads
    // dependencies by default, so --nodeps is what turns that off for a
    // download-only-the-named-packages request.
    QStringList args = {"pacman", "-Sw", "--noconfirm"};
    if (!includeDependencies)
        args << "--nodeps";
    if (!destinationDir.isEmpty())
        args << QStringLiteral("--cachedir=%1").arg(destinationDir);
    args += packageNames;
    // Still needs pkexec: pacman takes a database lock for any -S
    // operation regardless of whether anything will actually be
    // installed, even when the target cache directory is user-writable.
    return {{"pkexec", args}};
}

OperationResult PacmanBackend::downloadPackages(const QStringList &packageNames, const QString &destinationDir,
                                                 bool includeDependencies)
{
    const Result result =
        ProcessRunner::runSequence(downloadCommands(packageNames, destinationDir, includeDependencies));
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

OperationResult PacmanBackend::addRepository(const RepositoryAddValues &values)
{
    const QString name = values.value("name").trimmed();
    const QString server = values.value("server").trimmed();

    OperationResult op;
    if (name.isEmpty() || server.isEmpty()) {
        op.output = QStringLiteral("Repository name and server URL are required");
        return op;
    }

    QStringList lines = readLines(kPacmanConfPath);
    if (lines.isEmpty()) {
        op.output = QStringLiteral("Could not read %1").arg(kPacmanConfPath);
        return op;
    }

    lines << QString() << (QStringLiteral("[%1]").arg(name)) << (QStringLiteral("Server = %1").arg(server));

    op.success = writeFileAsRoot(kPacmanConfPath, lines);
    if (!op.success)
        op.output = QStringLiteral("Failed to write %1 (are you authorized for admin actions?)").arg(kPacmanConfPath);
    return op;
}
