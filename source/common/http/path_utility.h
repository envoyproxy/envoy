#pragma once

#include "envoy/http/header_map.h"

namespace Envoy {
namespace Http {

/**
 * Path helper extracted from chromium project.
 */
class PathUtil {
public:
  // Returns if the normalization succeeds.
  // If it is successful, the param will be updated with the normalized path.
  static bool canonicalPath(HeaderEntry& path_header);
  // Merges two or more adjacent slashes in path part of URI into one.
  static void mergeSlashes(HeaderEntry& path_header);
};

} // namespace Http
} // namespace Envoy
