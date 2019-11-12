#pragma once

#include "envoy/filesystem/filesystem.h"

namespace Envoy {

void checkSysctlLong(const std::string&, long, int64_t, Filesystem::Instance&);

void checkPlatformSettings(Filesystem::Instance&);

} // namespace Envoy
