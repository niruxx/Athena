#include "BackendFactory.h"

#include "DistroSupport.h"
#include "ProcessRunner.h"
#include "backends/AptBackend.h"
#include "backends/DnfBackend.h"
#include "backends/PacmanBackend.h"

namespace BackendFactory {

std::unique_ptr<PackageBackend> createForHostSystem()
{
    const DistroSupport::DistroFamily family = DistroSupport::detectDistroFamily();

    if (family == DistroSupport::DistroFamily::Fedora) {
        auto backend = std::make_unique<DnfBackend>();
        if (backend->isAvailable())
            return backend;
    }
    if (family == DistroSupport::DistroFamily::Debian) {
        auto backend = std::make_unique<AptBackend>();
        if (backend->isAvailable())
            return backend;
    }
    if (family == DistroSupport::DistroFamily::Arch) {
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
