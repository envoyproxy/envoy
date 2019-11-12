#pragma once

#include "envoy/filesystem/filesystem.h"

namespace Envoy {

// Check system wide configurations such as user level
// file watches, file limit permissions etc...
// TODO(windows maintainers)
static void check_platform_settings(Filesystem::Instance&) {
  std::cerr << "platform specific checks are not implemented for win32 - please ensure that "
               "adequate limits are set for the envoy process - for example number of max open "
               "files and file watches"
            << std::endl;
}
} // namespace Envoy
