#pragma once

#include <list>

#include "envoy/http/header_map.h"
#include "envoy/type/http/v3/path_transformation.pb.h"

#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

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
  // Removes the query and/or fragment string (if present) from the input path.
  // For example, this function returns "/data" for the input path "/data?param=value#fragment".
  static absl::string_view removeQueryAndFragment(const absl::string_view path);
};

class PathTransformer {
public:
  PathTransformer(envoy::type::http::v3::PathTransformation const& path_transformation);

  // Take a string_view as argument and return an optional string.
  // The optional will be null if the transformation fail.
  absl::optional<std::string> transform(const absl::string_view original_path) const;

  static absl::optional<std::string> mergeSlashes(absl::string_view original_path);

  static absl::optional<std::string> rfcNormalize(absl::string_view original_path);

private:
  using Transformation = std::function<absl::optional<std::string>(absl::string_view)>;
  // A sequence of transformations specified by path_transformation.operations()
  // Transformations will be applied to a path string in order in transform().
  std::list<Transformation> transformations_;
};

} // namespace Http
} // namespace Envoy
