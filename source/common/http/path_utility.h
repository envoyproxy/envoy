#pragma once

#include "envoy/http/header_map.h"
#include "envoy/runtime/runtime.h"

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
  static bool canonicalPath(RequestHeaderMap& headers,
                            Runtime::Loader* runtime = Runtime::LoaderSingleton::getExisting());
  // Merges two or more adjacent slashes in path part of URI into one.
  // Requires the Path header be present.
  static void mergeSlashes(RequestHeaderMap& headers);
  // Removes the query and/or fragment string (if present) from the input path.
  // For example, this function returns "/data" for the input path "/data?param=value#fragment".
  static absl::string_view removeQueryAndFragment(const absl::string_view path);
};

} // namespace Http
} // namespace Envoy
