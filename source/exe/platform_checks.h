#pragma once

#include "envoy/filesystem/filesystem.h"

namespace Envoy {

namespace Platform {

void checkPlatformSettings(Filesystem::Instance&);

} // namespace Platform

} // namespace Envoy
