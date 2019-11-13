#pragma once

#include "envoy/filesystem/filesystem.h"

namespace Envoy {

// Check system wide configurations such as user level
// file watches, file limit permissions etc...
// TODO(windows maintainers)
void checkPlatformSettings(Filesystem::Instance&) {
  ENVOY_LOG_MISC(
      warn,
      "platform specific checks are not implemented for win32 - please ensure that adequate limits "
      "are set for the envoy process - for example number of max open files and file watches");
}

} // namespace Envoy
