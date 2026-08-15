#include "DnfBackend.h"

#include <algorithm>

#include <QMap>
#include <QRegularExpression>

#include "../ProcessRunner.h"

using ProcessRunner::Result;

QString DnfBackend::dnfExecutable() const
{
    if (ProcessRunner::executableExists("dnf5"))
        return "dnf5";
    return "dnf";
}

QString DnfBackend::backendName() const
{
    return "DNF (Fedora / RHEL)";
}

bool DnfBackend::isAvailable() const
{
    return ProcessRunner::executableExists("dnf5") || ProcessRunner::executableExists("dnf");
}

QVector<PackageInfo> DnfBackend::listInstalled()
{
    QVector<PackageInfo> packages;

    // rpm queries the local database directly; it's far faster than asking
    // dnf to list installed packages since dnf also touches repo metadata.
    const Result result = ProcessRunner::run(
        "rpm",
        {"-qa", "--queryformat", "%{NAME}\t%{VERSION}-%{RELEASE}\t%{ARCH}\t%{SUMMARY}\n"});

    const QStringList lines = result.stdOut.split('\n', Qt::SkipEmptyParts);
    packages.reserve(lines.size());
    for (const QString &line : lines) {
        const QStringList fields = line.split('\t');
        if (fields.size() < 4)
            continue;
        PackageInfo pkg;
        pkg.name = fields[0];
        pkg.installedVersion = fields[1];
        pkg.architecture = fields[2];
        pkg.description = fields[3];
        pkg.repository = "installed";
        pkg.installed = true;
        packages.append(pkg);
    }

    std::sort(packages.begin(), packages.end(),
              [](const PackageInfo &a, const PackageInfo &b) { return a.name < b.name; });
    return packages;
}

QVector<PackageInfo> DnfBackend::search(const QString &query)
{
    QVector<PackageInfo> results;

    const Result searchResult = ProcessRunner::run(dnfExecutable(), {"search", query}, 60000);

    // Cross-reference against installed packages so search results can be
    // flagged as already-installed without a per-package rpm query.
    QMap<QString, PackageInfo> installedByName;
    for (const PackageInfo &pkg : listInstalled())
        installedByName.insert(pkg.name, pkg);

    const QStringList lines = searchResult.stdOut.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (!line.contains('\t'))
            continue; // "Matched fields: ..." section headers

        const int tabIndex = line.indexOf('\t');
        QString nameArch = line.left(tabIndex).trimmed();
        const QString summary = line.mid(tabIndex + 1).trimmed();

        QString arch;
        const int dotIndex = nameArch.lastIndexOf('.');
        if (dotIndex > 0) {
            arch = nameArch.mid(dotIndex + 1);
            nameArch.truncate(dotIndex);
        }

        PackageInfo pkg;
        pkg.name = nameArch;
        pkg.architecture = arch;
        pkg.description = summary;

        const auto installedIt = installedByName.constFind(pkg.name);
        if (installedIt != installedByName.constEnd()) {
            pkg.installed = true;
            pkg.installedVersion = installedIt->installedVersion;
            pkg.repository = "installed";
        } else {
            pkg.repository = "available";
        }

        results.append(pkg);
    }

    return results;
}

namespace {

// Parses dnf5's "Key             : value" / continuation-line blocks (used
// by `group info` and `environment info`) into an ordered key -> values map.
QMap<QString, QStringList> parseKeyValueBlock(const QString &text)
{
    QMap<QString, QStringList> fields;
    QString lastKey;
    bool sawContent = false;

    const QStringList lines = text.split('\n');
    for (const QString &rawLine : lines) {
        if (rawLine.trimmed().isEmpty()) {
            // dnf5 prints one blank-line-separated record per matching
            // repo (e.g. once for the installed @System copy, once for
            // the remote repo copy) when a group/environment is known to
            // more than one. Only the first record is wanted; the rest
            // would just duplicate every field.
            if (sawContent)
                break;
            continue;
        }

        const int colonIndex = rawLine.indexOf(':');
        if (colonIndex < 0)
            continue;
        sawContent = true;

        const QString key = rawLine.left(colonIndex).trimmed();
        const QString value = rawLine.mid(colonIndex + 1).trimmed();

        if (key.isEmpty()) {
            if (!lastKey.isEmpty() && !value.isEmpty())
                fields[lastKey].append(value);
            continue;
        }

        lastKey = key;
        if (!value.isEmpty())
            fields[key].append(value);
        else if (!fields.contains(key))
            fields[key] = {};
    }

    return fields;
}

// Parses the fixed-column "ID  Name  Installed" table shared by
// `group list` and `environment list`. A group/environment known to more
// than one repo (e.g. installed from @System but also available from a
// remote repo) is listed once per repo; rows are merged by id so each one
// only appears once, installed if any of its rows say so.
QVector<PackageGroupInfo> parseGroupTable(const QString &text, bool isMeta)
{
    QVector<PackageGroupInfo> groups;
    QMap<QString, int> indexById;
    // Columns are normally 2+ spaces apart, but dnf5 only guarantees a
    // single space when the id/name overflows its allotted column width
    // (e.g. long copr repo ids) — \s+ (not \s{2,}) so those rows still match.
    static const QRegularExpression rowPattern(QStringLiteral(R"(^(\S+)\s+(.+?)\s+(yes|no)\s*$)"));

    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QRegularExpressionMatch match = rowPattern.match(line);
        if (!match.hasMatch())
            continue; // header row or stray output

        const QString id = match.captured(1).trimmed();
        const bool installed = (match.captured(3) == "yes");

        const auto it = indexById.constFind(id);
        if (it != indexById.constEnd()) {
            groups[it.value()].installed = groups[it.value()].installed || installed;
            continue;
        }

        PackageGroupInfo group;
        group.id = id;
        group.name = match.captured(2).trimmed();
        group.installed = installed;
        group.isMeta = isMeta;
        indexById[id] = groups.size();
        groups.append(group);
    }
    return groups;
}

// Parses `dnf5 info <names...>` output: one blank-line-separated
// "Key : value" block per matching package, grouped under an "Installed
// packages" or "Available packages" section header. Unlike
// parseKeyValueBlock (which only wants the first record), every block here
// is significant — a package can legitimately appear in both sections at
// once (installed, but with a newer version available).
QVector<PackageInfo> parsePackageInfoBlocks(const QString &text)
{
    QMap<QString, PackageInfo> byName;
    bool inAvailableSection = false;
    QMap<QString, QStringList> currentFields;
    QString lastKey;

    auto flushBlock = [&]() {
        if (currentFields.isEmpty())
            return;
        const QString name = currentFields.value("Name").value(0);
        if (name.isEmpty()) {
            currentFields.clear();
            lastKey.clear();
            return;
        }

        PackageInfo &pkg = byName[name];
        pkg.name = name;

        const QString version = currentFields.value("Version").value(0);
        const QString release = currentFields.value("Release").value(0);
        const QString fullVersion = release.isEmpty() ? version : version + "-" + release;
        if (inAvailableSection) {
            pkg.availableVersion = fullVersion;
        } else {
            pkg.installedVersion = fullVersion;
            pkg.installed = true;
        }

        if (pkg.architecture.isEmpty())
            pkg.architecture = currentFields.value("Architecture").value(0);

        const QString repo = currentFields.contains("From repository")
            ? currentFields.value("From repository").value(0)
            : currentFields.value("Repository").value(0);
        if (!repo.isEmpty())
            pkg.repository = repo;

        if (pkg.description.isEmpty())
            pkg.description = currentFields.value("Summary").value(0);
        if (pkg.longDescription.isEmpty())
            pkg.longDescription = currentFields.value("Description").join(' ');

        currentFields.clear();
        lastKey.clear();
    };

    for (const QString &rawLine : text.split('\n')) {
        const QString trimmedLine = rawLine.trimmed();
        if (trimmedLine.compare("Installed packages", Qt::CaseInsensitive) == 0) {
            flushBlock();
            inAvailableSection = false;
            continue;
        }
        if (trimmedLine.compare("Available packages", Qt::CaseInsensitive) == 0) {
            flushBlock();
            inAvailableSection = true;
            continue;
        }
        if (trimmedLine.isEmpty()) {
            flushBlock();
            continue;
        }

        const int colonIndex = rawLine.indexOf(':');
        if (colonIndex < 0)
            continue;
        const QString key = rawLine.left(colonIndex).trimmed();
        const QString value = rawLine.mid(colonIndex + 1).trimmed();
        if (key.isEmpty()) {
            if (!lastKey.isEmpty() && !value.isEmpty())
                currentFields[lastKey].append(value);
            continue;
        }
        lastKey = key;
        if (!value.isEmpty())
            currentFields[key].append(value);
    }
    flushBlock();

    return byName.values();
}

// Strips the repo-loading boilerplate and trailing "aborted" line dnf5
// prints around a `--assumeno` transaction preview, leaving just the
// package table + summary that's actually useful to show the user.
QString cleanDnfPlanText(const QString &rawOutput)
{
    QStringList kept;
    for (const QString &line : rawOutput.split('\n')) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed == QLatin1String("Updating and loading repositories:")
            || trimmed == QLatin1String("Repositories loaded.")
            || trimmed == QLatin1String("Operation aborted by the user."))
            continue;
        kept << line;
    }
    return kept.join('\n').trimmed();
}

} // namespace

QVector<PackageGroupInfo> DnfBackend::listGroups()
{
    QVector<PackageGroupInfo> groups;

    const Result envResult = ProcessRunner::run(dnfExecutable(), {"environment", "list"}, 60000);
    groups += parseGroupTable(envResult.stdOut, /*isMeta=*/true);

    const Result groupResult = ProcessRunner::run(dnfExecutable(), {"group", "list"}, 60000);
    groups += parseGroupTable(groupResult.stdOut, /*isMeta=*/false);

    return groups;
}

PackageGroupInfo DnfBackend::groupDetails(const QString &groupId, bool isMeta)
{
    PackageGroupInfo group;
    group.id = groupId;
    group.isMeta = isMeta;

    const QStringList args = {isMeta ? "environment" : "group", "info", groupId};
    const Result result = ProcessRunner::run(dnfExecutable(), args, 30000);
    const QMap<QString, QStringList> fields = parseKeyValueBlock(result.stdOut);

    if (fields.contains("Name") && !fields["Name"].isEmpty())
        group.name = fields["Name"].first();
    if (fields.contains("Description") && !fields["Description"].isEmpty())
        group.description = fields["Description"].join(' ');
    if (fields.contains("Installed") && !fields["Installed"].isEmpty())
        group.installed = fields["Installed"].first().compare("yes", Qt::CaseInsensitive) == 0
            || fields["Installed"].first().compare("true", Qt::CaseInsensitive) == 0;

    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
        const QString lowerKey = it.key().toLower();
        if (isMeta && lowerKey.contains("groups"))
            group.subGroups += it.value();
        else if (!isMeta && lowerKey.contains("packages"))
            group.packages += it.value();
    }

    return group;
}

QVector<PackageInfo> DnfBackend::packageDetails(const QStringList &packageNames)
{
    if (packageNames.isEmpty())
        return {};

    QStringList args = {"info"};
    args += packageNames;
    const Result result = ProcessRunner::run(dnfExecutable(), args, 60000);
    return parsePackageInfoBlocks(result.stdOut);
}

QVector<PackageInfo> DnfBackend::listUpdates()
{
    QVector<PackageInfo> updates;

    const Result result = ProcessRunner::run(dnfExecutable(), {"list", "--upgrades"}, 60000);

    QMap<QString, PackageInfo> installedByName;
    for (const PackageInfo &pkg : listInstalled())
        installedByName.insert(pkg.name, pkg);

    for (const QString &line : result.stdOut.split('\n', Qt::SkipEmptyParts)) {
        const QStringList fields = line.trimmed().split(' ', Qt::SkipEmptyParts);
        // "name.arch version repo"; skip anything that isn't shaped like
        // that (e.g. a stray section header), version always starts with
        // a digit under RPM versioning conventions.
        if (fields.size() < 3 || !fields[1].at(0).isDigit())
            continue;

        QString nameArch = fields[0];
        QString arch;
        const int dotIndex = nameArch.lastIndexOf('.');
        if (dotIndex > 0) {
            arch = nameArch.mid(dotIndex + 1);
            nameArch.truncate(dotIndex);
        }

        PackageInfo pkg;
        pkg.name = nameArch;
        pkg.architecture = arch;
        pkg.availableVersion = fields[1];
        pkg.repository = fields[2];
        pkg.installed = true;

        const auto installedIt = installedByName.constFind(pkg.name);
        if (installedIt != installedByName.constEnd()) {
            pkg.installedVersion = installedIt->installedVersion;
            pkg.description = installedIt->description;
        }

        updates.append(pkg);
    }
    return updates;
}

OperationResult DnfBackend::upgradePackages(const QStringList &packageNames)
{
    QStringList args = {dnfExecutable(), "upgrade", "-y"};
    args += packageNames;
    const Result result = ProcessRunner::run("pkexec", args, 600000);
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

OperationResult DnfBackend::refreshMetadata()
{
    const Result result = ProcessRunner::run("pkexec", {dnfExecutable(), "makecache"}, 120000);
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

QVector<ProcessRunner::Command> DnfBackend::installCommands(const QStringList &packageNames) const
{
    QStringList args = {dnfExecutable(), "install", "-y"};
    args += packageNames;
    return {{"pkexec", args}};
}

QVector<ProcessRunner::Command> DnfBackend::removeCommands(const QStringList &packageNames) const
{
    // clean_requirements_on_remove is dnf5's default already, but set it
    // explicitly so removing a package also removes its now-unused
    // dependencies regardless of the user's dnf5.conf.
    QStringList args = {dnfExecutable(), "remove", "-y", "--setopt=clean_requirements_on_remove=True"};
    args += packageNames;
    return {{"pkexec", args}};
}

QVector<ProcessRunner::Command> DnfBackend::reinstallCommands(const QStringList &packageNames) const
{
    QStringList args = {dnfExecutable(), "reinstall", "-y"};
    args += packageNames;
    return {{"pkexec", args}};
}

OperationResult DnfBackend::installPackages(const QStringList &packageNames)
{
    const Result result = ProcessRunner::runSequence(installCommands(packageNames));
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

OperationResult DnfBackend::removePackages(const QStringList &packageNames)
{
    const Result result = ProcessRunner::runSequence(removeCommands(packageNames));
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

OperationResult DnfBackend::reinstallPackages(const QStringList &packageNames)
{
    const Result result = ProcessRunner::runSequence(reinstallCommands(packageNames));
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

TransactionPreview DnfBackend::previewInstall(const QStringList &packageNames)
{
    QStringList args = {"--assumeno", "install"};
    args += packageNames;
    const Result result = ProcessRunner::run(dnfExecutable(), args, 60000);

    TransactionPreview preview;
    preview.available = result.started;
    preview.planText = cleanDnfPlanText(result.stdOut);
    return preview;
}

TransactionPreview DnfBackend::previewRemove(const QStringList &packageNames)
{
    QStringList args = {"--assumeno", "remove", "--setopt=clean_requirements_on_remove=True"};
    args += packageNames;
    const Result result = ProcessRunner::run(dnfExecutable(), args, 60000);

    TransactionPreview preview;
    preview.available = result.started;
    preview.planText = cleanDnfPlanText(result.stdOut);
    return preview;
}

QVector<ProcessRunner::Command> DnfBackend::cleanUnusedDependenciesCommands() const
{
    return {{"pkexec", {dnfExecutable(), "autoremove", "-y"}}};
}

OperationResult DnfBackend::cleanUnusedDependencies()
{
    const Result result = ProcessRunner::runSequence(cleanUnusedDependenciesCommands(), 300000);
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

QString DnfBackend::recentHistory()
{
    const Result result = ProcessRunner::run(dnfExecutable(), {"history", "list"}, 30000);
    return result.stdOut.trimmed();
}

QVector<RepositoryInfo> DnfBackend::listRepositories()
{
    QVector<RepositoryInfo> repos;

    const Result result = ProcessRunner::run(dnfExecutable(), {"repo", "list", "--all"}, 30000);
    // \s+ rather than \s{2,}: a long repo id (e.g. copr:...) can overflow
    // its column and leave only a single space before the name column.
    static const QRegularExpression rowPattern(QStringLiteral(R"(^(\S+)\s+(.+?)\s+(enabled|disabled)\s*$)"));

    for (const QString &line : result.stdOut.split('\n', Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch match = rowPattern.match(line);
        if (!match.hasMatch())
            continue; // header row

        RepositoryInfo repo;
        repo.id = match.captured(1).trimmed();
        repo.name = match.captured(2).trimmed();
        repo.enabled = (match.captured(3) == "enabled");
        repos.append(repo);
    }
    return repos;
}

OperationResult DnfBackend::setRepositoryEnabled(const QString &repoId, bool enabled)
{
    const QStringList args = {dnfExecutable(), "config-manager", enabled ? "enable" : "disable", repoId};
    const Result result = ProcessRunner::run("pkexec", args, 60000);
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

QVector<RepositoryAddField> DnfBackend::repositoryAddFields() const
{
    return {
        {"id", "Repository ID", "myrepo", true},
        {"baseurl", "Base URL", "https://example.com/repo/", true},
        {"name", "Display Name (optional)", "My Repo", false},
    };
}

OperationResult DnfBackend::addRepository(const RepositoryAddValues &values)
{
    const QString id = values.value("id").trimmed();
    const QString baseUrl = values.value("baseurl").trimmed();
    const QString name = values.value("name").trimmed();

    OperationResult op;
    if (id.isEmpty() || baseUrl.isEmpty()) {
        op.output = QStringLiteral("Repository ID and base URL are required");
        return op;
    }

    QStringList args = {dnfExecutable(), "config-manager", "addrepo", "--id=" + id,
                         "--set=baseurl=" + baseUrl};
    if (!name.isEmpty())
        args << "--set=name=" + name;

    const Result result = ProcessRunner::run("pkexec", args, 60000);
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}
