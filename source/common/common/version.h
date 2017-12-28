#pragma once

#include <string>

namespace Envoy {
// Release version (e.g. 1.5.0).
static const std::string& ReleaseVersion = "1.5.0";

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
