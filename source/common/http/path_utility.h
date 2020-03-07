#pragma once

#include "envoy/http/header_map.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {

/**
 * Path helper extracted from chromium project.
 */
class PathUtil {
public:
  // Returns if the normalization succeeds.
  // If it is successful, the path header in header path will be updated with the normalized path.
  static bool canonicalPath(RequestHeaderMap& headers);
  // Merges two or more adjacent slashes in path part of URI into one.
  static void mergeSlashes(RequestHeaderMap& headers);
  // Removes the query and/or fragment string (if present) from the input path.
  // For example, this function returns "/data" for the input path "/data#fragment?param=value".
  static absl::string_view removeQueryAndFragment(const absl::string_view path);
};

} // namespace Http
} // namespace Envoy
