#include "common/http/path_utility.h"

#include "envoy/common/exception.h"

#include "common/common/logger.h"
#include "common/http/legacy_path_canonicalizer.h"
#include "common/runtime/runtime_features.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/types/optional.h"
#include "url/url_canon.h"
#include "url/url_canon_stdstring.h"

namespace Envoy {
namespace Http {

namespace {
absl::optional<std::string> canonicalizePath(absl::string_view original_path) {
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.remove_forked_chromium_url")) {
    std::string canonical_path;
    url::Component in_component(0, original_path.size());
    url::Component out_component;
    url::StdStringCanonOutput output(&canonical_path);
    if (!url::CanonicalizePath(original_path.data(), in_component, &output, &out_component)) {
      return absl::nullopt;
    } else {
      output.Complete();
      return absl::make_optional(std::move(canonical_path));
    }
  }
  return LegacyPathCanonicalizer::canonicalizePath(original_path);
}
} // namespace

/* static */
bool PathUtil::canonicalPath(RequestHeaderMap& headers) {
  ASSERT(headers.Path());
  const auto original_path = headers.getPathValue();
  absl::optional<std::string> normalized_path = PathTransformer::rfcNormalize(original_path);
  if (!normalized_path.has_value()) {
    return false;
  }
  headers.setPath(normalized_path.value());
  return true;
}

void PathUtil::mergeSlashes(RequestHeaderMap& headers) {
  ASSERT(headers.Path());
  const auto original_path = headers.getPathValue();
  absl::optional<std::string> normalized = PathTransformer::mergeSlashes(original_path).value();
  if (normalized.has_value()) {
    headers.setPath(normalized.value());
  }
}

absl::string_view PathUtil::removeQueryAndFragment(const absl::string_view path) {
  absl::string_view ret = path;
  // Trim query parameters and/or fragment if present.
  size_t offset = ret.find_first_of("?#");
  if (offset != absl::string_view::npos) {
    ret.remove_suffix(ret.length() - offset);
  }
  return ret;
}

absl::optional<std::string> PathTransformer::mergeSlashes(absl::string_view original_path) {
  const absl::string_view::size_type query_start = original_path.find('?');
  const absl::string_view path = original_path.substr(0, query_start);
  const absl::string_view query = absl::ClippedSubstr(original_path, query_start);
  if (path.find("//") == absl::string_view::npos) {
    return std::string(original_path);
  }
  const absl::string_view path_prefix = absl::StartsWith(path, "/") ? "/" : absl::string_view();
  const absl::string_view path_suffix = absl::EndsWith(path, "/") ? "/" : absl::string_view();
  return absl::StrCat(path_prefix, absl::StrJoin(absl::StrSplit(path, '/', absl::SkipEmpty()), "/"),
                      path_suffix, query);
}

absl::optional<std::string> PathTransformer::rfcNormalize(absl::string_view original_path) {
  const auto query_pos = original_path.find('?');
  auto normalized_path_opt = canonicalizePath(
      query_pos == original_path.npos
          ? original_path
          : absl::string_view(original_path.data(), query_pos) // '?' is not included
  );
  if (!normalized_path_opt.has_value()) {
    return {};
  }
  auto& normalized_path = normalized_path_opt.value();
  const absl::string_view query_suffix =
      query_pos == original_path.npos
          ? absl::string_view{}
          : absl::string_view{original_path.data() + query_pos, original_path.size() - query_pos};
  if (!query_suffix.empty()) {
    normalized_path.insert(normalized_path.end(), query_suffix.begin(), query_suffix.end());
  }
  return normalized_path;
}

PathTransformer::PathTransformer(envoy::type::http::v3::PathTransformation path_transformation) {
  const auto& operations = path_transformation.operations();
  std::vector<uint64_t> operation_hashes;
  for (auto const& operation : operations) {
    uint64_t operation_hash = MessageUtil::hash(operation);
    if (find(operation_hashes.begin(), operation_hashes.end(), operation_hash) !=
        operation_hashes.end()) {
      throw EnvoyException("Duplicate path transformation");
    }
    if (operation.has_normalize_path_rfc_3986()) {
      transformations_.emplace_back(PathTransformer::rfcNormalize);
    } else if (operation.has_merge_slashes()) {
      transformations_.emplace_back(PathTransformer::mergeSlashes);
    }
    operation_hashes.push_back(operation_hash);
  }
}

absl::optional<std::string> PathTransformer::transform(absl::string_view original) const {
  absl::optional<std::string> path_string;
  absl::string_view path_string_view = original;
  for (Transformation const& transformation : transformations_) {
    path_string = transformation(path_string_view);
    if (!path_string.has_value()) {
      return {};
    }
    path_string_view = path_string.value();
  }
  return path_string;
}

} // namespace Http
} // namespace Envoy
