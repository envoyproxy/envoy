#pragma once

#include <string>

#include "common/common/version_number.h"

namespace Envoy {

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
