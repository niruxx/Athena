#include "AptBackend.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QTextStream>

#include "../ProcessRunner.h"

using ProcessRunner::Result;

QString AptBackend::backendName() const
{
    return "APT (Debian / Ubuntu)";
}

bool AptBackend::isAvailable() const
{
    return ProcessRunner::executableExists("apt-cache") && ProcessRunner::executableExists("dpkg-query");
}

QVector<PackageInfo> AptBackend::listInstalled()
{
    QVector<PackageInfo> packages;

    const Result result = ProcessRunner::run(
        "dpkg-query",
        {"-W", "-f", "${Package}\t${Version}\t${Architecture}\t${Status}\t${binary:Summary}\n"});

    const QStringList lines = result.stdOut.split('\n', Qt::SkipEmptyParts);
    packages.reserve(lines.size());
    for (const QString &line : lines) {
        const QStringList fields = line.split('\t');
        if (fields.size() < 4)
            continue;
        if (!fields[3].contains("installed") || fields[3].contains("not-installed"))
            continue;

        PackageInfo pkg;
        pkg.name = fields[0];
        pkg.installedVersion = fields[1];
        pkg.architecture = fields[2];
        pkg.description = fields.size() > 4 ? fields[4] : QString();
        pkg.repository = "installed";
        pkg.installed = true;
        packages.append(pkg);
    }

    std::sort(packages.begin(), packages.end(),
              [](const PackageInfo &a, const PackageInfo &b) { return a.name < b.name; });
    return packages;
}

QVector<PackageInfo> AptBackend::search(const QString &query)
{
    QVector<PackageInfo> results;

    const Result result = ProcessRunner::run("apt-cache", {"search", query}, 60000);

    QMap<QString, PackageInfo> installedByName;
    for (const PackageInfo &pkg : listInstalled())
        installedByName.insert(pkg.name, pkg);

    static const QRegularExpression linePattern(QStringLiteral(R"(^(\S+)\s+-\s+(.*)$)"));
    for (const QString &line : result.stdOut.split('\n', Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch match = linePattern.match(line);
        if (!match.hasMatch())
            continue;

        PackageInfo pkg;
        pkg.name = match.captured(1);
        pkg.description = match.captured(2);

        const auto installedIt = installedByName.constFind(pkg.name);
        if (installedIt != installedByName.constEnd()) {
            pkg.installed = true;
            pkg.installedVersion = installedIt->installedVersion;
            pkg.architecture = installedIt->architecture;
            pkg.repository = "installed";
        } else {
            pkg.repository = "available";
        }

        results.append(pkg);
    }

    return results;
}

namespace {

struct AptVersions {
    QString installed;
    QString candidate;
};

// Parses `apt-cache policy <names...>`: one "pkgname:" header per package
// followed by indented "Installed:"/"Candidate:" lines. Candidate is the
// latest version apt would install — present (and equal to Installed) even
// when there's no pending upgrade, unlike dnf5's list/info output.
QMap<QString, AptVersions> parseAptCachePolicy(const QString &text)
{
    QMap<QString, AptVersions> result;
    QString currentName;

    for (const QString &rawLine : text.split('\n')) {
        if (!rawLine.isEmpty() && !rawLine.startsWith(' ') && rawLine.endsWith(':')) {
            currentName = rawLine.left(rawLine.length() - 1).trimmed();
            continue;
        }
        if (currentName.isEmpty())
            continue;

        const QString trimmed = rawLine.trimmed();
        if (trimmed.startsWith("Installed:")) {
            const QString value = trimmed.mid(QString("Installed:").length()).trimmed();
            if (value != "(none)")
                result[currentName].installed = value;
        } else if (trimmed.startsWith("Candidate:")) {
            const QString value = trimmed.mid(QString("Candidate:").length()).trimmed();
            if (value != "(none)")
                result[currentName].candidate = value;
        }
    }
    return result;
}

struct AptDescription {
    QString summary;
    QString longDescription;
};

// Parses `apt-cache show <names...>`: Debian control-file stanzas
// separated by blank lines. Only the first stanza per package name is
// kept (a name can have several across repos/versions; descriptions
// rarely differ meaningfully between them).
QMap<QString, AptDescription> parseAptCacheShow(const QString &text)
{
    QMap<QString, AptDescription> result;
    QString currentName;
    QStringList descriptionLines;
    bool inDescription = false;

    auto flushStanza = [&]() {
        if (!currentName.isEmpty() && !descriptionLines.isEmpty() && !result.contains(currentName)) {
            AptDescription desc;
            desc.summary = descriptionLines.first();
            QStringList body = descriptionLines.mid(1);
            for (QString &line : body) {
                if (line == QLatin1String("."))
                    line.clear(); // "." is Debian control-file notation for a blank paragraph line
            }
            desc.longDescription = body.join(' ').simplified();
            result[currentName] = desc;
        }
        currentName.clear();
        descriptionLines.clear();
        inDescription = false;
    };

    for (const QString &rawLine : text.split('\n')) {
        if (rawLine.isEmpty()) {
            flushStanza();
            continue;
        }
        if (rawLine.startsWith("Package:")) {
            currentName = rawLine.mid(QString("Package:").length()).trimmed();
            inDescription = false;
            continue;
        }
        if (rawLine.startsWith("Description:") || rawLine.startsWith("Description-en:")) {
            const int colonIndex = rawLine.indexOf(':');
            descriptionLines = {rawLine.mid(colonIndex + 1).trimmed()};
            inDescription = true;
            continue;
        }
        if (inDescription && rawLine.startsWith(' ')) {
            descriptionLines.append(rawLine.trimmed());
            continue;
        }
        inDescription = false;
    }
    flushStanza();

    return result;
}

} // namespace

QVector<PackageInfo> AptBackend::packageDetails(const QStringList &packageNames)
{
    if (packageNames.isEmpty())
        return {};

    QStringList policyArgs = {"policy"};
    policyArgs += packageNames;
    const Result policyResult = ProcessRunner::run("apt-cache", policyArgs, 60000);
    const QMap<QString, AptVersions> versions = parseAptCachePolicy(policyResult.stdOut);

    QStringList showArgs = {"show"};
    showArgs += packageNames;
    const Result showResult = ProcessRunner::run("apt-cache", showArgs, 60000);
    const QMap<QString, AptDescription> descriptions = parseAptCacheShow(showResult.stdOut);

    QVector<PackageInfo> results;
    for (const QString &name : packageNames) {
        if (!versions.contains(name) && !descriptions.contains(name))
            continue;

        PackageInfo pkg;
        pkg.name = name;

        const AptVersions v = versions.value(name);
        pkg.installedVersion = v.installed;
        pkg.availableVersion = v.candidate;
        pkg.installed = !v.installed.isEmpty();
        pkg.repository = pkg.installed ? "installed" : "available";

        const AptDescription d = descriptions.value(name);
        pkg.description = d.summary;
        pkg.longDescription = d.longDescription;

        results.append(pkg);
    }
    return results;
}

namespace {

constexpr QChar kIdSep(0x1F); // unit separator; won't collide with real path/text content

QStringList sourceFilePaths()
{
    QStringList paths;
    if (QFileInfo::exists("/etc/apt/sources.list"))
        paths << "/etc/apt/sources.list";

    QDir sourcesDir("/etc/apt/sources.list.d");
    const QStringList entries = sourcesDir.entryList({"*.list", "*.sources"}, QDir::Files, QDir::Name);
    for (const QString &entry : entries)
        paths << sourcesDir.filePath(entry);

    return paths;
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

// Extracts the URI token from a classic "deb [options] URI suite comps..."
// line (with any leading "# " already stripped).
QString extractClassicUri(const QString &activeLine)
{
    const QStringList tokens = activeLine.split(' ', Qt::SkipEmptyParts);
    bool insideOptionsGroup = false;
    for (int i = 1; i < tokens.size(); ++i) {
        if (insideOptionsGroup) {
            if (tokens[i].endsWith(']'))
                insideOptionsGroup = false;
            continue;
        }
        if (tokens[i].startsWith('[')) {
            // "[arch=amd64 signed-by=/x.gpg]" can itself contain spaces and
            // so span multiple whitespace-separated tokens; only stop
            // skipping once the token that closes the bracket is reached.
            if (!tokens[i].endsWith(']'))
                insideOptionsGroup = true;
            continue;
        }
        return tokens[i];
    }
    return QString();
}

// One-line "deb"/"deb-src" entries in classic sources.list(.d/*.list) files.
QVector<RepositoryInfo> parseClassicFile(const QString &path)
{
    QVector<RepositoryInfo> repos;
    const QStringList lines = readLines(path);

    static const QRegularExpression entryPattern(QStringLiteral(R"(^(#\s*)?(deb|deb-src)\s+(.*)$)"));
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines[i].trimmed();
        const QRegularExpressionMatch match = entryPattern.match(trimmed);
        if (!match.hasMatch())
            continue;

        const bool commented = match.capturedLength(1) > 0;
        const QString activeLine = match.captured(2) + ' ' + match.captured(3);

        RepositoryInfo repo;
        repo.id = QStringLiteral("L") + kIdSep + path + kIdSep + QString::number(i);
        repo.name = activeLine;
        repo.url = extractClassicUri(activeLine);
        repo.enabled = !commented;
        repos.append(repo);
    }
    return repos;
}

// Deb822-style *.sources files: blank-line-separated "Key: Value" stanzas.
// A stanza is enabled unless it has an explicit "Enabled: no" field.
QVector<RepositoryInfo> parseDeb822File(const QString &path)
{
    QVector<RepositoryInfo> repos;
    const QStringList lines = readLines(path);

    int stanzaStart = -1;
    QMap<QString, QString> fields;

    auto flush = [&]() {
        if (stanzaStart < 0 || !fields.contains("Types"))
            return;
        RepositoryInfo repo;
        repo.id = QStringLiteral("S") + kIdSep + path + kIdSep + QString::number(stanzaStart);
        repo.name = fields.value("Suites", fields.value("URIs"));
        repo.url = fields.value("URIs");
        repo.enabled = fields.value("Enabled", "yes").compare("no", Qt::CaseInsensitive) != 0;
        repos.append(repo);
        stanzaStart = -1;
        fields.clear();
    };

    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i];
        if (line.trimmed().isEmpty()) {
            flush();
            continue;
        }
        if (stanzaStart < 0)
            stanzaStart = i;

        const int colonIndex = line.indexOf(':');
        if (colonIndex > 0)
            fields[line.left(colonIndex).trimmed()] = line.mid(colonIndex + 1).trimmed();
    }
    flush();

    return repos;
}

bool writeFileAsRoot(const QString &path, const QStringList &lines)
{
    const QByteArray content = (lines.join('\n') + '\n').toUtf8();
    const Result result = ProcessRunner::runWithStdin("pkexec", {"tee", path}, content, 30000);
    return result.started && result.exitCode == 0;
}

OperationResult toggleClassicLine(const QString &path, int lineIndex, bool enabled)
{
    QStringList lines = readLines(path);
    OperationResult op;
    if (lineIndex < 0 || lineIndex >= lines.size()) {
        op.output = QStringLiteral("Repository entry no longer found in %1").arg(path);
        return op;
    }

    const QString trimmed = lines[lineIndex].trimmed();
    static const QRegularExpression entryPattern(QStringLiteral(R"(^(#\s*)?((?:deb|deb-src)\s+.*)$)"));
    const QRegularExpressionMatch match = entryPattern.match(trimmed);
    if (!match.hasMatch()) {
        op.output = QStringLiteral("Repository entry at %1:%2 no longer looks like a deb line")
                        .arg(path)
                        .arg(lineIndex + 1);
        return op;
    }

    lines[lineIndex] = enabled ? match.captured(2) : (QStringLiteral("# ") + match.captured(2));

    op.success = writeFileAsRoot(path, lines);
    if (!op.success)
        op.output = QStringLiteral("Failed to write %1 (are you authorized for admin actions?)").arg(path);
    return op;
}

OperationResult toggleDeb822Stanza(const QString &path, int stanzaStart, bool enabled)
{
    QStringList lines = readLines(path);
    OperationResult op;
    if (stanzaStart < 0 || stanzaStart >= lines.size()) {
        op.output = QStringLiteral("Repository entry no longer found in %1").arg(path);
        return op;
    }

    int stanzaEnd = stanzaStart;
    while (stanzaEnd < lines.size() && !lines[stanzaEnd].trimmed().isEmpty())
        ++stanzaEnd;

    int enabledLineIndex = -1;
    for (int i = stanzaStart; i < stanzaEnd; ++i) {
        if (lines[i].startsWith(QLatin1String("Enabled:"), Qt::CaseInsensitive)) {
            enabledLineIndex = i;
            break;
        }
    }

    const QString newLine = QStringLiteral("Enabled: %1").arg(enabled ? "yes" : "no");
    if (enabledLineIndex >= 0)
        lines[enabledLineIndex] = newLine;
    else
        lines.insert(stanzaStart + 1, newLine); // right after the first line (Types:) of the stanza

    op.success = writeFileAsRoot(path, lines);
    if (!op.success)
        op.output = QStringLiteral("Failed to write %1 (are you authorized for admin actions?)").arg(path);
    return op;
}

} // namespace

QString AptBackend::recentHistory()
{
    QFile file(QStringLiteral("/var/log/apt/history.log"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("No history log found (expected at /var/log/apt/history.log).");

    QTextStream stream(&file);
    QStringList lines;
    while (!stream.atEnd())
        lines << stream.readLine();

    // Newest first, capped to a reasonable amount of recent activity.
    std::reverse(lines.begin(), lines.end());
    constexpr int maxLines = 500;
    if (lines.size() > maxLines)
        lines = lines.mid(0, maxLines);

    return lines.join('\n');
}

QVector<RepositoryInfo> AptBackend::listRepositories()
{
    QVector<RepositoryInfo> repos;
    for (const QString &path : sourceFilePaths()) {
        if (path.endsWith(".sources"))
            repos += parseDeb822File(path);
        else
            repos += parseClassicFile(path);
    }
    return repos;
}

OperationResult AptBackend::setRepositoryEnabled(const QString &repoId, bool enabled)
{
    const QStringList parts = repoId.split(kIdSep);
    OperationResult op;
    if (parts.size() != 3) {
        op.output = QStringLiteral("Malformed repository id");
        return op;
    }

    const QString &kind = parts[0];
    const QString &path = parts[1];
    const int lineIndex = parts[2].toInt();

    if (kind == QLatin1String("S"))
        return toggleDeb822Stanza(path, lineIndex, enabled);
    return toggleClassicLine(path, lineIndex, enabled);
}

QVector<RepositoryAddField> AptBackend::repositoryAddFields() const
{
    return {
        {"uri", "Repository URL", "https://example.com/debian", true},
        {"suite", "Suite/Distribution", "stable", true},
        {"components", "Components", "main", true},
    };
}

OperationResult AptBackend::addRepository(const RepositoryAddValues &values)
{
    const QString uri = values.value("uri").trimmed();
    const QString suite = values.value("suite").trimmed();
    const QString components = values.value("components").trimmed();

    OperationResult op;
    if (uri.isEmpty() || suite.isEmpty() || components.isEmpty()) {
        op.output = QStringLiteral("Repository URL, suite, and components are required");
        return op;
    }

    // A new dedicated file (rather than appending to sources.list) so this
    // addition is easy to find and remove later, matching how third-party
    // repos are conventionally added on Debian/Ubuntu.
    QString slug = uri;
    slug.remove(QRegularExpression(QStringLiteral("^[a-z]+://")));
    slug.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9.-]+")), QStringLiteral("-"));
    if (slug.isEmpty())
        slug = QStringLiteral("custom");
    const QString path = QStringLiteral("/etc/apt/sources.list.d/distore-%1.list").arg(slug);

    const QString line = QStringLiteral("deb %1 %2 %3").arg(uri, suite, components);
    op.success = writeFileAsRoot(path, {line});
    if (!op.success)
        op.output = QStringLiteral("Failed to write %1 (are you authorized for admin actions?)").arg(path);
    return op;
}

QVector<PackageGroupInfo> AptBackend::listGroups()
{
    QVector<PackageGroupInfo> groups;
    if (!ProcessRunner::executableExists("tasksel"))
        return groups;

    const Result result = ProcessRunner::run("tasksel", {"--list-tasks"}, 30000);

    static const QRegularExpression linePattern(QStringLiteral(R"(^([iu])\s+(\S+)\s+(.*)$)"));
    for (const QString &line : result.stdOut.split('\n', Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch match = linePattern.match(line);
        if (!match.hasMatch())
            continue;

        PackageGroupInfo group;
        group.id = match.captured(2);
        group.name = match.captured(2);
        group.description = match.captured(3).trimmed();
        group.installed = (match.captured(1) == "i");
        group.isMeta = true;
        groups.append(group);
    }

    return groups;
}

PackageGroupInfo AptBackend::groupDetails(const QString &groupId, bool /*isMeta*/)
{
    PackageGroupInfo group;
    group.id = groupId;
    group.name = groupId;
    group.isMeta = true;

    // Tasksel tasks are backed by a task-<id> metapackage; its direct
    // dependencies/recommendations are the packages the task pulls in.
    const QString metapackage = "task-" + groupId;
    const Result result = ProcessRunner::run("apt-cache", {"depends", metapackage}, 30000);

    static const QRegularExpression depPattern(
        QStringLiteral(R"(^\s*(?:Depends|Recommends):\s*<?([A-Za-z0-9.+\-]+)>?\s*$)"));
    for (const QString &line : result.stdOut.split('\n', Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch match = depPattern.match(line);
        if (!match.hasMatch())
            continue;
        const QString pkgName = match.captured(1);
        if (!group.packages.contains(pkgName))
            group.packages.append(pkgName);
    }

    QVector<PackageInfo> installed = listInstalled();
    group.installed = std::any_of(installed.begin(), installed.end(), [&](const PackageInfo &pkg) {
        return pkg.name == metapackage;
    });

    return group;
}

QVector<PackageInfo> AptBackend::listUpdates()
{
    QVector<PackageInfo> updates;

    const Result result = ProcessRunner::run("apt", {"list", "--upgradable"}, 60000);
    static const QRegularExpression pattern(
        QStringLiteral(R"(^(\S+?)/\S+\s+(\S+)\s+(\S+)\s+\[upgradable from:\s*([^\]]+)\])"));

    for (const QString &line : result.stdOut.split('\n', Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch match = pattern.match(line);
        if (!match.hasMatch())
            continue;

        PackageInfo pkg;
        pkg.name = match.captured(1);
        pkg.availableVersion = match.captured(2);
        pkg.architecture = match.captured(3);
        pkg.installedVersion = match.captured(4).trimmed();
        pkg.installed = true;
        pkg.repository = QStringLiteral("installed");
        updates.append(pkg);
    }
    return updates;
}

OperationResult AptBackend::upgradePackages(const QStringList &packageNames)
{
    QStringList args = {"env", "DEBIAN_FRONTEND=noninteractive", "apt-get", "install", "-y", "--only-upgrade"};
    args += packageNames;
    const Result result = ProcessRunner::run("pkexec", args, 600000);
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

OperationResult AptBackend::refreshMetadata()
{
    const Result result = ProcessRunner::run(
        "pkexec", {"env", "DEBIAN_FRONTEND=noninteractive", "apt-get", "update"}, 120000);
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

QVector<ProcessRunner::Command> AptBackend::installCommands(const QStringList &packageNames) const
{
    // DEBIAN_FRONTEND=noninteractive suppresses debconf prompts that would
    // otherwise hang waiting for input apt-get can't get through pkexec.
    QStringList args = {"env", "DEBIAN_FRONTEND=noninteractive", "apt-get", "install", "-y"};
    args += packageNames;
    return {{"pkexec", args}};
}

QVector<ProcessRunner::Command> AptBackend::removeCommands(const QStringList &packageNames) const
{
    // --auto-remove folds cleanup of now-unused dependencies into the
    // same transaction, instead of leaving them for a separate autoremove.
    QStringList args = {"env", "DEBIAN_FRONTEND=noninteractive", "apt-get", "remove", "-y", "--auto-remove"};
    args += packageNames;
    return {{"pkexec", args}};
}

QVector<ProcessRunner::Command> AptBackend::reinstallCommands(const QStringList &packageNames) const
{
    QStringList args = {"env", "DEBIAN_FRONTEND=noninteractive", "apt-get", "install", "-y", "--reinstall"};
    args += packageNames;
    return {{"pkexec", args}};
}

OperationResult AptBackend::installPackages(const QStringList &packageNames)
{
    const Result result = ProcessRunner::runSequence(installCommands(packageNames));
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

OperationResult AptBackend::removePackages(const QStringList &packageNames)
{
    const Result result = ProcessRunner::runSequence(removeCommands(packageNames));
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

OperationResult AptBackend::reinstallPackages(const QStringList &packageNames)
{
    const Result result = ProcessRunner::runSequence(reinstallCommands(packageNames));
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

namespace {

// Strips apt-get's "Reading package lists... Done" style progress noise,
// keeping the "following packages will be ..." summary that's actually
// useful in a preview.
QString cleanAptPlanText(const QString &rawOutput)
{
    QStringList kept;
    for (const QString &line : rawOutput.split('\n')) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.endsWith(QLatin1String("... Done")))
            continue;
        kept << line;
    }
    return kept.join('\n').trimmed();
}

} // namespace

TransactionPreview AptBackend::previewInstall(const QStringList &packageNames)
{
    QStringList args = {"install", "--dry-run"};
    args += packageNames;
    const Result result = ProcessRunner::run("apt-get", args, 60000);

    TransactionPreview preview;
    preview.available = result.started;
    preview.planText = cleanAptPlanText(result.stdOut);
    return preview;
}

TransactionPreview AptBackend::previewRemove(const QStringList &packageNames)
{
    QStringList args = {"remove", "--dry-run", "--auto-remove"};
    args += packageNames;
    const Result result = ProcessRunner::run("apt-get", args, 60000);

    TransactionPreview preview;
    preview.available = result.started;
    preview.planText = cleanAptPlanText(result.stdOut);
    return preview;
}

QVector<ProcessRunner::Command> AptBackend::cleanUnusedDependenciesCommands() const
{
    return {{"pkexec", {"env", "DEBIAN_FRONTEND=noninteractive", "apt-get", "autoremove", "-y"}}};
}

OperationResult AptBackend::cleanUnusedDependencies()
{
    const Result result = ProcessRunner::runSequence(cleanUnusedDependenciesCommands(), 300000);
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}
