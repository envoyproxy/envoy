#pragma once

#include "envoy/filesystem/filesystem.h"

namespace Envoy {

namespace Platform {

/**
 * Check system wide configurations such as user level
 * file watches, file limit permissions etc...
 */
void checkPlatformSettings(Filesystem::Instance&);

} // namespace Platform

} // namespace Envoy
