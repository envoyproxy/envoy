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
  // Returns true if the normalization succeeds.
  // If it is successful, the path header will be updated with the normalized path.
  // Requires the Path header be present.
  static bool canonicalPath(RequestHeaderMap& headers);
  // Merges two or more adjacent slashes in path part of URI into one.
  // Requires the Path header be present.
  static void mergeSlashes(RequestHeaderMap& headers);

  enum class UnescapeSlashesResult {
    // No escaped slash sequences were found and URL path has not been modified.
    NotFound = 0,
    // Escaped slash sequences were found and URL path has been modified.
    FoundAndUnescaped = 1,
  };
  // Unescape %2F, %2f, %5C and %5c sequences.
  // Requires the Path header be present.
  // Returns the result of unescaping slashes.
  static UnescapeSlashesResult unescapeSlashes(RequestHeaderMap& headers);
  // Removes the query and/or fragment string (if present) from the input path.
  // For example, this function returns "/data" for the input path "/data?param=value#fragment".
  static absl::string_view removeQueryAndFragment(const absl::string_view path);
};

} // namespace Http
} // namespace Envoy
