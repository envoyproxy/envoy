#pragma once

#include <list>

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.validate.h"
#include "envoy/http/header_map.h"
#include "envoy/type/http/v3/path_transformation.pb.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

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

enum class NormalizePathAction {
  Continue = 0,
  Reject = 1,
  Redirect = 2,
};

class PathTransformer {
public:
  PathTransformer() = default;
  PathTransformer(const envoy::extensions::filters::network::http_connection_manager::v3::
                      HttpConnectionManager::PathWithEscapedSlashesAction escaped_slashes_action,
                  const bool should_normalize_path, const bool should_merge_slashes);
  PathTransformer(envoy::type::http::v3::PathTransformation const& path_transformation);

  // Take a string_view as argument and return an optional string.
  // The optional will be null if the transformation fail.
  absl::optional<std::string> transform(const absl::string_view original_path,
                                        NormalizePathAction& normalize_path_action) const;

  static absl::optional<std::string> mergeSlashes(absl::string_view original_path);

  static absl::optional<std::string> rfcNormalize(absl::string_view original_path);

  static absl::optional<std::string> unescapeSlashes(absl::string_view original_path);

private:
  using Transformation = std::function<absl::optional<std::string>(absl::string_view)>;
  // A sequence of transformations specified by path_transformation.operations()
  // Transformations will be applied to a path string in order in transform().
  std::vector<Transformation> transformations_;
  std::vector<NormalizePathAction> normalize_path_actions_;
};

} // namespace Http
} // namespace Envoy
