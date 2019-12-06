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
  // If it is successful, the path header in header path will be updated with the normalized path.
  static bool canonicalPath(HeaderMap& headers);
  // Merges two or more adjacent slashes in path part of URI into one.
  static void mergeSlashes(HeaderMap& headers);
};

} // namespace Http
} // namespace Envoy
