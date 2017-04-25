#pragma once

#include <string>

/**
 * Wraps compiled in code versioning.
 * NOTE: The GIT_SHA is generated into version_generated.cc and is not present in the source tree.
 */
class VersionInfo {
public:
  static std::string version();

  static const std::string GIT_SHA;
};
