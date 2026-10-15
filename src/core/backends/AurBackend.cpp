#include "AurBackend.h"

#include <algorithm>
#include <functional>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include <QUrlQuery>

#include "../AppSettings.h"
#include "../ProcessRunner.h"
#include "../Version.h"

using ProcessRunner::Result;

namespace {

const QString kAurBaseUrl = QStringLiteral("https://aur.archlinux.org");

// AUR builds can legitimately take a very long time (browsers, kernels),
// far beyond ProcessRunner::runSequence()'s default per-command timeout.
constexpr int kBuildTimeoutMs = 4 * 60 * 60 * 1000;

// The AUR caps request URI length; 100 names per "info" call stays well
// under it even for long package names.
constexpr int kInfoBatchSize = 100;

// Guards against pathological dependency chains (or a cycle the visited
// set somehow misses) turning resolution into an endless network crawl.
constexpr int kMaxDependencyDepth = 25;

// The AUR's own package-name rule. Validated before a name is ever used in
// a URL or a filesystem path (the makepkg fallback `rm -rf`s a per-package
// directory under the cache before cloning into it).
bool isValidPackageName(const QString &name)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^[a-z0-9@_+][a-z0-9@._+-]*$)"));
    return pattern.match(name).hasMatch();
}

// "foo>=1.2" / "foo=1.2" / "foo<2" -> "foo".
QString dependencyName(const QString &dependency)
{
    static const QRegularExpression versionOperator(QStringLiteral("[<>=]"));
    const int index = dependency.indexOf(versionOperator);
    return (index >= 0 ? dependency.left(index) : dependency).trimmed();
}

// Blocking GET through curl rather than QNetworkAccessManager: curl falls
// back from an unreachable IPv6 route to IPv4 promptly, where Qt's HTTP
// stack sits out its whole timeout first. curl is always present on Arch,
// since pacman itself depends on it.
QByteArray httpGet(const QUrl &url, QString *errorOut)
{
    const Result result = ProcessRunner::run(
        "curl",
        {"--fail", "--silent", "--show-error", "--location", "--connect-timeout", "10", "--max-time", "30",
         "--user-agent", QStringLiteral("Athena/%1").arg(ATHENA_VERSION), "--", url.toString(QUrl::FullyEncoded)},
        40000);
    if (!result.started || result.exitCode != 0) {
        if (errorOut)
            *errorOut = result.started ? result.stdErr.trimmed() : QObject::tr("couldn't run curl");
        return {};
    }
    return result.stdOut.toUtf8();
}

QStringList toStringList(const QJsonValue &value)
{
    QStringList list;
    for (const QJsonValue &entry : value.toArray())
        list << entry.toString();
    return list;
}

AurBackend::AurPackage parseAurPackage(const QJsonObject &object)
{
    AurBackend::AurPackage pkg;
    pkg.name = object.value("Name").toString();
    pkg.packageBase = object.value("PackageBase").toString();
    if (pkg.packageBase.isEmpty())
        pkg.packageBase = pkg.name;
    pkg.version = object.value("Version").toString();
    pkg.description = object.value("Description").toString();
    pkg.url = object.value("URL").toString();
    pkg.maintainer = object.value("Maintainer").toString();
    pkg.numVotes = object.value("NumVotes").toInt();
    pkg.popularity = object.value("Popularity").toDouble();
    pkg.outOfDate = object.value("OutOfDate").toInteger();
    pkg.depends = toStringList(object.value("Depends"));
    pkg.makeDepends = toStringList(object.value("MakeDepends"));
    pkg.checkDepends = toStringList(object.value("CheckDepends"));
    return pkg;
}

// Parses an RPC response body, returning its "results" (empty on an
// "error"-type response, e.g. a too-short or too-broad search term).
QVector<AurBackend::AurPackage> parseRpcResults(const QByteArray &body, QString *errorOut)
{
    QVector<AurBackend::AurPackage> packages;
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    if (root.value("type").toString() == QLatin1String("error")) {
        if (errorOut)
            *errorOut = root.value("error").toString();
        return packages;
    }
    for (const QJsonValue &entry : root.value("results").toArray())
        packages.append(parseAurPackage(entry.toObject()));
    return packages;
}

// RPC "info" for an explicit set of names, batched; names the AUR doesn't
// know are simply absent from the result.
QMap<QString, AurBackend::AurPackage> rpcInfo(const QStringList &names)
{
    QMap<QString, AurBackend::AurPackage> byName;
    QStringList valid;
    for (const QString &name : names) {
        if (isValidPackageName(name))
            valid << name;
    }
    valid.removeDuplicates();

    for (int offset = 0; offset < valid.size(); offset += kInfoBatchSize) {
        QUrlQuery query;
        for (const QString &name : valid.mid(offset, kInfoBatchSize))
            query.addQueryItem(QStringLiteral("arg[]"), name);
        QUrl url(kAurBaseUrl + QStringLiteral("/rpc/v5/info"));
        url.setQuery(query);

        for (const AurBackend::AurPackage &pkg : parseRpcResults(httpGet(url, nullptr), nullptr))
            byName.insert(pkg.name, pkg);
    }
    return byName;
}

// RPC "search" with the given `by` field (name-desc, provides, depends...),
// most popular first.
QVector<AurBackend::AurPackage> rpcSearch(const QString &term, const QString &by)
{
    QUrl url(kAurBaseUrl + QStringLiteral("/rpc/v5/search/")
             + QString::fromLatin1(QUrl::toPercentEncoding(term)));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("by"), by);
    url.setQuery(query);

    QVector<AurBackend::AurPackage> results = parseRpcResults(httpGet(url, nullptr), nullptr);
    std::sort(results.begin(), results.end(), [](const AurBackend::AurPackage &a, const AurBackend::AurPackage &b) {
        return a.popularity > b.popularity;
    });
    return results;
}

QString aurGitUrl(const QString &packageBase)
{
    return QStringLiteral("%1/%2.git").arg(kAurBaseUrl, packageBase);
}

QString buildCacheRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/aur");
}

// A step that prints `message` to stderr and fails, so a problem found
// while computing commands (e.g. a package that isn't on the AUR) shows
// up in the terminal dialog and stops the sequence, rather than vanishing.
ProcessRunner::Command failCommand(const QString &message)
{
    return {QStringLiteral("sh"),
            {QStringLiteral("-c"), QStringLiteral("printf 'error: %s\\n' \"$1\" >&2; exit 1"),
             QStringLiteral("athena"), message}};
}

OperationResult toOperationResult(const Result &result)
{
    OperationResult op;
    op.success = result.started && result.exitCode == 0;
    op.output = result.stdOut + result.stdErr;
    return op;
}

// `pacman -T` prints whichever of the given dependency strings (version
// constraints and provides included) aren't satisfied by installed
// packages.
QStringList unsatisfiedDependencies(const QStringList &dependencies)
{
    if (dependencies.isEmpty())
        return {};
    const Result result = ProcessRunner::run("pacman", QStringList{"-T", "--"} + dependencies, 30000);
    return result.stdOut.split('\n', Qt::SkipEmptyParts);
}

// Whether a sync repository can provide `name` (directly or via provides).
bool availableInSyncRepos(const QString &name)
{
    const Result result =
        ProcessRunner::run("pacman", {"-Sp", "--noconfirm", "--nodeps", "--print-format", "%n", "--", name}, 30000);
    return result.started && result.exitCode == 0;
}

// makepkg refuses to run as root, and calls PACMAN_AUTH (instead of sudo,
// which can't prompt without a terminal) for its --syncdeps and --install
// steps.
ProcessRunner::Command makepkgCommand(const QString &buildDir, bool asDependency, bool reinstall)
{
    QStringList args = {"PACMAN_AUTH=pkexec", "makepkg", "--syncdeps", "--install", "--noconfirm", "--cleanbuild"};
    if (asDependency)
        args << "--asdeps" << "--needed";
    else if (!reinstall)
        args << "--needed";
    return {"env", args, buildDir};
}

QString describeAurPackage(const AurBackend::AurPackage &pkg)
{
    QStringList notes;
    notes << QObject::tr("%n vote(s)", "", pkg.numVotes);
    notes << (pkg.maintainer.isEmpty() ? QObject::tr("ORPHANED") : QObject::tr("maintainer %1").arg(pkg.maintainer));
    if (pkg.outOfDate > 0)
        notes << QObject::tr("FLAGGED OUT OF DATE");
    return QStringLiteral("  %1 %2  (%3)").arg(pkg.name, pkg.version, notes.join(QStringLiteral(", ")));
}

} // namespace

QString AurBackend::backendName() const
{
    return "AUR (Arch User Repository)";
}

bool AurBackend::isAvailable() const
{
    return ProcessRunner::executableExists("pacman") && ProcessRunner::executableExists("makepkg");
}

AurBackend::BuildMethod AurBackend::buildMethod()
{
    if (ProcessRunner::executableExists("paru"))
        return BuildMethod::Paru;
    if (ProcessRunner::executableExists("yay"))
        return BuildMethod::Yay;
    return BuildMethod::Makepkg;
}

QString AurBackend::buildMethodName(BuildMethod method)
{
    switch (method) {
    case BuildMethod::Paru:
        return QObject::tr("paru (AUR helper)");
    case BuildMethod::Yay:
        return QObject::tr("yay (AUR helper)");
    case BuildMethod::Makepkg:
        break;
    }
    return QObject::tr("makepkg (built-in — install paru or yay for a full AUR helper)");
}

QVector<ProcessRunner::Command> AurBackend::helperInstallCommands(const QString &helper)
{
    if (helper != QLatin1String("yay") && helper != QLatin1String("paru"))
        return {};

    const QString cacheRoot = buildCacheRoot();
    const QString buildDir = cacheRoot + '/' + helper;
    return {
        {"pkexec", {"pacman", "-S", "--needed", "--noconfirm", "git", "base-devel"}},
        {"mkdir", {"-p", cacheRoot}},
        {"rm", {"-rf", "--", buildDir}},
        {"git", {"clone", "--depth=1", aurGitUrl(helper), buildDir}},
        // --syncdeps pulls in the helper's toolchain (go for yay, cargo
        // for paru) through pkexec, same as the regular makepkg fallback.
        {"env", {"PACMAN_AUTH=pkexec", "makepkg", "--syncdeps", "--install", "--noconfirm", "--cleanbuild"}, buildDir},
    };
}

QString AurBackend::helperOwningPackage(const QString &helper)
{
    if (helper != QLatin1String("yay") && helper != QLatin1String("paru"))
        return {};
    const QString path = QStandardPaths::findExecutable(helper);
    if (path.isEmpty())
        return {};
    const Result owner = ProcessRunner::run("pacman", {"-Qqo", "--", path}, 15000);
    if (!owner.started || owner.exitCode != 0)
        return {};
    return owner.stdOut.trimmed();
}

QVector<ProcessRunner::Command> AurBackend::helperRemoveCommands(const QString &helper)
{
    const QString package = helperOwningPackage(helper);
    if (package.isEmpty())
        return {};
    return {{"pkexec", {"pacman", "-Rs", "--noconfirm", "--", package}}};
}

QString AurBackend::fetchPkgbuild(const QString &packageName, QString *errorOut) const
{
    const QString name = packageName.trimmed();
    if (!isValidPackageName(name)) {
        if (errorOut)
            *errorOut = QObject::tr("\"%1\" isn't a valid AUR package name.").arg(name);
        return {};
    }

    const QMap<QString, AurPackage> info = rpcInfo({name});
    if (!info.contains(name)) {
        if (errorOut)
            *errorOut = QObject::tr("No AUR package named \"%1\" was found.").arg(name);
        return {};
    }

    QUrl url(kAurBaseUrl + QStringLiteral("/cgit/aur.git/plain/PKGBUILD"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("h"), info[name].packageBase);
    url.setQuery(query);
    return QString::fromUtf8(httpGet(url, errorOut));
}

QMap<QString, QString> AurBackend::installedForeignPackages()
{
    QMap<QString, QString> packages;
    const Result result = ProcessRunner::run("pacman", {"-Qm"}, 30000);
    for (const QString &line : result.stdOut.split('\n', Qt::SkipEmptyParts)) {
        const QStringList fields = line.split(' ', Qt::SkipEmptyParts);
        if (fields.size() >= 2)
            packages.insert(fields[0], fields[1]);
    }
    return packages;
}

QVector<PackageInfo> AurBackend::listInstalled()
{
    QVector<PackageInfo> packages = packageDetails(installedForeignPackages().keys());
    std::sort(packages.begin(), packages.end(),
              [](const PackageInfo &a, const PackageInfo &b) { return a.name < b.name; });
    return packages;
}

QVector<PackageInfo> AurBackend::search(const QString &query)
{
    QVector<PackageInfo> results;
    for (const AurPackage &aur : rpcSearch(query.trimmed(), QStringLiteral("name-desc"))) {
        PackageInfo pkg;
        pkg.name = aur.name;
        pkg.availableVersion = aur.version;
        pkg.description = aur.description;
        pkg.repository = QStringLiteral("aur");
        results.append(pkg);
    }
    return results;
}

QVector<PackageInfo> AurBackend::dependencyQuery(const QString &capability, bool findRequires)
{
    const QString trimmed = capability.trimmed();
    if (trimmed.isEmpty())
        return {};

    QStringList names;
    for (const AurPackage &pkg : rpcSearch(trimmed, findRequires ? QStringLiteral("depends") : QStringLiteral("provides")))
        names << pkg.name;
    return packageDetails(names);
}

QVector<PackageGroupInfo> AurBackend::listGroups()
{
    return {};
}

PackageGroupInfo AurBackend::groupDetails(const QString &groupId, bool isMeta)
{
    PackageGroupInfo group;
    group.id = groupId;
    group.name = groupId;
    group.isMeta = isMeta;
    return group;
}

QVector<PackageInfo> AurBackend::packageDetails(const QStringList &packageNames)
{
    if (packageNames.isEmpty())
        return {};

    const Result localResult = ProcessRunner::run("pacman", QStringList{"-Qi", "--"} + packageNames, 30000);
    const auto localBlocks = PacmanBackend::parseInfoBlocks(localResult.stdOut);
    const QMap<QString, AurPackage> aurInfo = rpcInfo(packageNames);

    QVector<PackageInfo> results;
    for (const QString &name : packageNames) {
        const bool installed = localBlocks.contains(name);
        const bool onAur = aurInfo.contains(name);
        if (!installed && !onAur)
            continue;

        PackageInfo pkg;
        pkg.name = name;
        pkg.installed = installed;
        // "local" marks a foreign package that isn't (or is no longer) on
        // the AUR — built by hand, or deleted from the AUR since.
        pkg.repository = onAur ? QStringLiteral("aur") : QStringLiteral("local");

        if (installed) {
            const QMap<QString, QString> &fields = localBlocks[name];
            pkg.installedVersion = fields.value("Version");
            pkg.architecture = fields.value("Architecture");
            pkg.description = fields.value("Description");
            pkg.size = fields.value("Installed Size");
            const QString url = fields.value("URL");
            if (url != QLatin1String("None"))
                pkg.homepageUrl = url;
        }

        if (onAur) {
            const AurPackage &aur = aurInfo[name];
            pkg.availableVersion = aur.version;
            if (pkg.description.isEmpty())
                pkg.description = aur.description;
            if (pkg.homepageUrl.isEmpty())
                pkg.homepageUrl = aur.url;

            QStringList meta;
            meta << QObject::tr("Votes: %1").arg(aur.numVotes)
                 << QObject::tr("Popularity: %1").arg(aur.popularity, 0, 'f', 2)
                 << (aur.maintainer.isEmpty() ? QObject::tr("Orphaned (no maintainer)")
                                              : QObject::tr("Maintainer: %1").arg(aur.maintainer));
            QString longDescription = pkg.description + QStringLiteral("\n\n") + meta.join(QStringLiteral("  ·  "));
            if (aur.outOfDate > 0) {
                longDescription += QStringLiteral("\n")
                    + QObject::tr("Flagged out of date on %1")
                          .arg(QDateTime::fromSecsSinceEpoch(aur.outOfDate).date().toString(Qt::ISODate));
            }
            longDescription += QStringLiteral("\n") + QObject::tr("AUR page: %1/packages/%2").arg(kAurBaseUrl, name);
            pkg.longDescription = longDescription;
        } else {
            pkg.longDescription =
                pkg.description + QStringLiteral("\n\n") + QObject::tr("Not found on the AUR (built locally or removed).");
        }

        results.append(pkg);
    }
    return results;
}

AurBackend::BuildPlan AurBackend::resolveBuildPlan(const QStringList &targets)
{
    BuildPlan plan;
    const QMap<QString, AurPackage> targetInfo = rpcInfo(targets);
    const QSet<QString> targetSet(targets.begin(), targets.end());

    QSet<QString> visitedBases;
    QSet<QString> checkedDependencies;

    std::function<void(const AurPackage &, int)> visit = [&](const AurPackage &pkg, int depth) {
        if (visitedBases.contains(pkg.packageBase))
            return;
        visitedBases.insert(pkg.packageBase);

        if (depth < kMaxDependencyDepth) {
            QStringList aurNeeded;
            for (const QString &dependency : unsatisfiedDependencies(pkg.depends + pkg.makeDepends + pkg.checkDepends)) {
                const QString name = dependencyName(dependency);
                if (name.isEmpty() || checkedDependencies.contains(name))
                    continue;
                checkedDependencies.insert(name);
                if (availableInSyncRepos(name))
                    plan.repoDependencies << name;
                else
                    aurNeeded << name;
            }

            const QMap<QString, AurPackage> byName = rpcInfo(aurNeeded);
            for (const QString &name : aurNeeded) {
                if (byName.contains(name)) {
                    visit(byName[name], depth + 1);
                    continue;
                }
                // Not an AUR package name itself; maybe something on the
                // AUR provides it (e.g. "java-runtime").
                const QVector<AurPackage> providers = rpcSearch(name, QStringLiteral("provides"));
                if (providers.isEmpty()) {
                    plan.unresolved << name;
                    continue;
                }
                const QMap<QString, AurPackage> providerInfo = rpcInfo({providers.first().name});
                if (providerInfo.isEmpty())
                    plan.unresolved << name;
                else
                    visit(providerInfo.first(), depth + 1);
            }
        }

        // Post-order: everything this package needs is already queued.
        plan.toBuild.append(pkg);
        if (!targetSet.contains(pkg.name))
            plan.dependencyNames.insert(pkg.name);
    };

    for (const QString &target : targets) {
        if (targetInfo.contains(target))
            visit(targetInfo[target], 0);
        else
            plan.notFound << target;
    }
    return plan;
}

QVector<ProcessRunner::Command> AurBackend::buildCommands(const QStringList &packageNames, bool reinstall) const
{
    if (packageNames.isEmpty())
        return {};
    if (reviewRequired()) {
        return {failCommand(QObject::tr("AUR builds require PKGBUILD review first (Settings → General → AUR); "
                                        "refusing to build unreviewed."))};
    }

    const BuildMethod method = buildMethod();
    if (method == BuildMethod::Paru) {
        // pkexec instead of sudo: there's no terminal for sudo to prompt
        // in. --skipreview because review is an interactive pager —
        // Athena's PKGBUILD tab is where to review instead.
        QStringList args = {"-S", "--aur", "--noconfirm", "--skipreview", "--sudo", "pkexec"};
        if (!reinstall)
            args << "--needed";
        return {{"paru", args + packageNames}};
    }
    if (method == BuildMethod::Yay) {
        QStringList args = {"-S",           "--aur",          "--noconfirm",    "--answerclean", "None",
                            "--answerdiff", "None",           "--answeredit",   "None",          "--removemake",
                            "--sudo",       "pkexec",         "--nosudoloop"};
        if (!reinstall)
            args << "--needed";
        return {{"yay", args + packageNames}};
    }

    if (!ProcessRunner::executableExists("git"))
        return {failCommand(QObject::tr("git is required to fetch AUR packages (install it with pacman -S git)."))};

    const BuildPlan plan = resolveBuildPlan(packageNames);
    if (!plan.notFound.isEmpty())
        return {failCommand(QObject::tr("not found on the AUR: %1").arg(plan.notFound.join(", ")))};

    const QString cacheRoot = buildCacheRoot();
    QVector<ProcessRunner::Command> commands;
    commands.append({"mkdir", {"-p", cacheRoot}});

    for (const AurPackage &pkg : plan.toBuild) {
        if (!isValidPackageName(pkg.packageBase))
            return {failCommand(QObject::tr("refusing unexpected package base name \"%1\"").arg(pkg.packageBase))};
        const QString buildDir = cacheRoot + '/' + pkg.packageBase;

        // Always a fresh clone, so a previous half-finished build (or a
        // PKGBUILD edited by hand) can't leak into this one.
        commands.append({"rm", {"-rf", "--", buildDir}});
        commands.append({"git", {"clone", "--depth=1", aurGitUrl(pkg.packageBase), buildDir}});
        commands.append(makepkgCommand(buildDir, plan.dependencyNames.contains(pkg.name), reinstall));
    }
    return commands;
}

bool AurBackend::reviewRequired()
{
    return AppSettings::instance().reviewAurBuilds();
}

AurBackend::PreparedBuild AurBackend::prepareReviewedBuild(const QStringList &packageNames)
{
    PreparedBuild build;
    if (!ProcessRunner::executableExists("git")) {
        build.error = QObject::tr("git is required to fetch AUR packages (install it with pacman -S git).");
        return build;
    }

    const BuildPlan plan = resolveBuildPlan(packageNames);
    build.repoDependencies = plan.repoDependencies;
    build.unresolved = plan.unresolved;
    build.notFound = plan.notFound;
    if (!plan.notFound.isEmpty()) {
        build.error = QObject::tr("Not found on the AUR: %1").arg(plan.notFound.join(", "));
        return build;
    }

    const QString cacheRoot = buildCacheRoot();
    QDir().mkpath(cacheRoot);
    for (const AurPackage &pkg : plan.toBuild) {
        if (!isValidPackageName(pkg.packageBase)) {
            build.error = QObject::tr("Refusing unexpected package base name \"%1\".").arg(pkg.packageBase);
            return build;
        }

        // Always a fresh clone, so neither a half-finished earlier build
        // nor an earlier review's edits leak into what's reviewed now.
        const QString buildDir = cacheRoot + '/' + pkg.packageBase;
        QDir(buildDir).removeRecursively();
        const Result clone = ProcessRunner::run(
            "git", {"clone", "--depth=1", "--", aurGitUrl(pkg.packageBase), buildDir}, 120000);
        if (!clone.started || clone.exitCode != 0) {
            build.error = QObject::tr("Couldn't download the build files for %1:\n%2")
                              .arg(pkg.packageBase, clone.stdErr.trimmed());
            return build;
        }

        build.packages.append({pkg, buildDir, plan.dependencyNames.contains(pkg.name)});
    }
    return build;
}

QVector<ProcessRunner::Command> AurBackend::reviewedBuildCommands(const PreparedBuild &build, bool reinstall)
{
    QVector<ProcessRunner::Command> commands;
    for (const PreparedBuild::Package &pkg : build.packages)
        commands.append(makepkgCommand(pkg.directory, pkg.asDependency, reinstall));
    return commands;
}

QVector<ProcessRunner::Command> AurBackend::installCommands(const QStringList &packageNames) const
{
    return buildCommands(packageNames, false);
}

QVector<ProcessRunner::Command> AurBackend::reinstallCommands(const QStringList &packageNames) const
{
    return buildCommands(packageNames, true);
}

QVector<ProcessRunner::Command> AurBackend::removeCommands(const QStringList &packageNames) const
{
    return m_pacman.removeCommands(packageNames);
}

OperationResult AurBackend::installPackages(const QStringList &packageNames)
{
    return toOperationResult(ProcessRunner::runSequence(installCommands(packageNames), kBuildTimeoutMs));
}

OperationResult AurBackend::removePackages(const QStringList &packageNames)
{
    return m_pacman.removePackages(packageNames);
}

OperationResult AurBackend::reinstallPackages(const QStringList &packageNames)
{
    return toOperationResult(ProcessRunner::runSequence(reinstallCommands(packageNames), kBuildTimeoutMs));
}

TransactionPreview AurBackend::previewInstall(const QStringList &packageNames)
{
    const BuildPlan plan = resolveBuildPlan(packageNames);

    QStringList targetLines;
    QStringList dependencyLines;
    for (const AurPackage &pkg : plan.toBuild)
        (plan.dependencyNames.contains(pkg.name) ? dependencyLines : targetLines) << describeAurPackage(pkg);

    QStringList sections;
    sections << QObject::tr("Build method: %1").arg(buildMethodName(buildMethod()));
    if (!targetLines.isEmpty())
        sections << QObject::tr("AUR packages to build and install:\n%1").arg(targetLines.join('\n'));
    if (!dependencyLines.isEmpty())
        sections << QObject::tr("AUR dependencies to build first:\n%1").arg(dependencyLines.join('\n'));
    if (!plan.repoDependencies.isEmpty())
        sections << QObject::tr("Repository dependencies (installed by pacman):\n  %1")
                        .arg(plan.repoDependencies.join(QStringLiteral("\n  ")));
    if (!plan.notFound.isEmpty())
        sections << QObject::tr("Not found on the AUR:\n  %1").arg(plan.notFound.join(QStringLiteral("\n  ")));
    if (!plan.unresolved.isEmpty())
        sections << QObject::tr("Dependencies that couldn't be resolved (the build will likely fail):\n  %1")
                        .arg(plan.unresolved.join(QStringLiteral("\n  ")));
    sections << QObject::tr("AUR packages are user-submitted build scripts, not reviewed or signed by Arch "
                            "Linux. Turn on PKGBUILD review in Settings → General → AUR to read and edit "
                            "each one before it builds.");

    TransactionPreview preview;
    preview.available = true;
    preview.planText = sections.join(QStringLiteral("\n\n"));
    return preview;
}

TransactionPreview AurBackend::previewRemove(const QStringList &packageNames)
{
    return m_pacman.previewRemove(packageNames);
}

OperationResult AurBackend::cleanUnusedDependencies()
{
    return m_pacman.cleanUnusedDependencies();
}

QVector<ProcessRunner::Command> AurBackend::cleanUnusedDependenciesCommands() const
{
    return m_pacman.cleanUnusedDependenciesCommands();
}

QVector<PackageInfo> AurBackend::listUpdates()
{
    const QMap<QString, QString> installed = installedForeignPackages();
    const QMap<QString, AurPackage> aurInfo = rpcInfo(installed.keys());

    QVector<PackageInfo> updates;
    for (auto it = installed.constBegin(); it != installed.constEnd(); ++it) {
        if (!aurInfo.contains(it.key()))
            continue;
        const AurPackage &aur = aurInfo[it.key()];
        if (aur.version == it.value())
            continue;

        // vercmp prints <0 / 0 / >0, like strcmp, for "installed vs AUR".
        const Result cmp = ProcessRunner::run("vercmp", {it.value(), aur.version}, 5000);
        if (cmp.stdOut.trimmed().toInt() >= 0)
            continue;

        PackageInfo pkg;
        pkg.name = it.key();
        pkg.installedVersion = it.value();
        pkg.availableVersion = aur.version;
        pkg.description = aur.description;
        pkg.homepageUrl = aur.url;
        pkg.repository = QStringLiteral("aur");
        pkg.installed = true;
        updates.append(pkg);
    }
    return updates;
}

OperationResult AurBackend::upgradePackages(const QStringList &packageNames)
{
    return toOperationResult(ProcessRunner::runSequence(buildCommands(packageNames, true), kBuildTimeoutMs));
}

OperationResult AurBackend::refreshMetadata()
{
    OperationResult op;
    op.success = true;
    op.output = QStringLiteral("AUR metadata is queried live; nothing to refresh.");
    return op;
}

QVector<ProcessRunner::Command> AurBackend::downloadCommands(const QStringList &packageNames,
                                                              const QString &destinationDir,
                                                              bool includeDependencies) const
{
    if (packageNames.isEmpty())
        return {};
    if (!ProcessRunner::executableExists("git"))
        return {failCommand(QObject::tr("git is required to fetch AUR packages (install it with pacman -S git)."))};

    QStringList bases;
    QStringList missing;
    if (includeDependencies) {
        const BuildPlan plan = resolveBuildPlan(packageNames);
        for (const AurPackage &pkg : plan.toBuild)
            bases << pkg.packageBase;
        missing = plan.notFound;
    } else {
        const QMap<QString, AurPackage> info = rpcInfo(packageNames);
        for (const QString &name : packageNames) {
            if (info.contains(name))
                bases << info[name].packageBase;
            else
                missing << name;
        }
    }
    if (!missing.isEmpty())
        return {failCommand(QObject::tr("not found on the AUR: %1").arg(missing.join(", ")))};
    bases.removeDuplicates();

    const QString destination = destinationDir.isEmpty() ? QDir::currentPath() : destinationDir;
    QVector<ProcessRunner::Command> commands;
    for (const QString &base : bases) {
        if (!isValidPackageName(base))
            continue;
        commands.append({"git", {"clone", aurGitUrl(base), destination + '/' + base}});
    }
    return commands;
}

OperationResult AurBackend::downloadPackages(const QStringList &packageNames, const QString &destinationDir,
                                              bool includeDependencies)
{
    return toOperationResult(
        ProcessRunner::runSequence(downloadCommands(packageNames, destinationDir, includeDependencies)));
}

QString AurBackend::recentHistory()
{
    QFile file(QStringLiteral("/var/log/pacman.log"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("No history log found (expected at /var/log/pacman.log).");

    const QMap<QString, QString> foreign = installedForeignPackages();
    QSet<QString> knownAurNames(foreign.keyBegin(), foreign.keyEnd());

    static const QRegularExpression actionPattern(
        QStringLiteral(R"(\[ALPM\] (installed|upgraded|reinstalled|downgraded|removed) (\S+) )"));

    QTextStream stream(&file);
    QStringList lines;
    bool inLocalInstall = false;
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.contains(QLatin1String("[PACMAN] Running '"))) {
            // makepkg and AUR helpers install what they built through
            // `pacman -U`; repository installs never do.
            inLocalInstall = line.contains(QLatin1String(" -U "));
            continue;
        }

        const QRegularExpressionMatch match = actionPattern.match(line);
        if (!match.hasMatch())
            continue;
        const QString name = match.captured(2);
        if (inLocalInstall)
            knownAurNames.insert(name);
        if (knownAurNames.contains(name))
            lines << line;
    }

    std::reverse(lines.begin(), lines.end());
    constexpr int maxLines = 500;
    if (lines.size() > maxLines)
        lines = lines.mid(0, maxLines);
    return lines.join('\n');
}

QVector<RepositoryInfo> AurBackend::listRepositories()
{
    return {};
}

OperationResult AurBackend::setRepositoryEnabled(const QString & /*repoId*/, bool /*enabled*/)
{
    OperationResult op;
    op.output = QStringLiteral("The AUR has no repositories to enable or disable.");
    return op;
}

QVector<RepositoryAddField> AurBackend::repositoryAddFields() const
{
    return {};
}

OperationResult AurBackend::addRepository(const RepositoryAddValues & /*values*/)
{
    OperationResult op;
    op.output = QStringLiteral("The AUR has no repositories to add.");
    return op;
}
