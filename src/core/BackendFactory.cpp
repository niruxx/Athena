#include "BackendFactory.h"

#include <QFile>
#include <QTextStream>

#include "ProcessRunner.h"
#include "backends/AptBackend.h"
#include "backends/DnfBackend.h"
#include "backends/PacmanBackend.h"

namespace {

QString readOsReleaseIds()
{
    QFile file("/etc/os-release");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    QString ids;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.startsWith("ID=") || line.startsWith("ID_LIKE="))
            ids += line.section('=', 1).remove('"').toLower() + ' ';
    }
    return ids;
}

} // namespace

namespace BackendFactory {

std::unique_ptr<PackageBackend> createForHostSystem()
{
    const QString ids = readOsReleaseIds();

    if (ids.contains("fedora") || ids.contains("rhel") || ids.contains("centos")) {
        auto backend = std::make_unique<DnfBackend>();
        if (backend->isAvailable())
            return backend;
    }
    if (ids.contains("debian") || ids.contains("ubuntu")) {
        auto backend = std::make_unique<AptBackend>();
        if (backend->isAvailable())
            return backend;
    }
    if (ids.contains("arch")) {
        auto backend = std::make_unique<PacmanBackend>();
        if (backend->isAvailable())
            return backend;
    }

    // /etc/os-release didn't give a confident match (or was unreadable);
    // fall back to whichever supported package manager is actually on PATH.
    if (auto backend = std::make_unique<DnfBackend>(); backend->isAvailable())
        return backend;
    if (auto backend = std::make_unique<AptBackend>(); backend->isAvailable())
        return backend;
    if (auto backend = std::make_unique<PacmanBackend>(); backend->isAvailable())
        return backend;

    return nullptr;
}

} // namespace BackendFactory
