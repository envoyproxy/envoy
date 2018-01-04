#pragma once

#include <string>

#include "common/common/version_number.h"

namespace Envoy {

// Release version number (e.g. 1.6.0).
static const std::string& VERSION_NUMBER = BUILD_VERSION_NUMBER;

/**
 * Wraps compiled in code versioning.
 */
class VersionInfo {
public:
  // Repository revision (e.g. git SHA1).
  static const std::string& revision();
  // Repository status (e.g. clean, modified).
  static const std::string& revisionStatus();
  // Repository information and build type.
  static std::string version();
};
} // namespace Envoy
