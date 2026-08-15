#pragma once

#include <memory>

#include "PackageBackend.h"

namespace BackendFactory {

// Picks a backend based on /etc/os-release (ID / ID_LIKE) with a fallback
// to probing for known package-manager executables. Returns nullptr if no
// supported package manager could be found.
std::unique_ptr<PackageBackend> createForHostSystem();

} // namespace BackendFactory
