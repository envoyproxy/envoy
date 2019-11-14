#pragma once

#include "envoy/filesystem/filesystem.h"

namespace Envoy {

// Check system wide configurations such as user level
// file watches, file limit permissions etc...
// TODO(windows maintainers)
void checkPlatformSettings(Filesystem::Instance&) {}

} // namespace Envoy
