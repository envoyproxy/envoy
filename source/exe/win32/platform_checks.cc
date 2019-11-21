#pragma once

#include "envoy/filesystem/filesystem.h"

namespace Envoy {

namespace Platform {

// TODO(#9025)
void checkPlatformSettings(Filesystem::Instance&) {}

} // namespace Platform

} // namespace Envoy
