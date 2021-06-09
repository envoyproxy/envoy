#include "source/common/http/path_utility.h"

#include "source/common/common/logger.h"
#include "source/common/http/legacy_path_canonicalizer.h"
#include "source/common/runtime/runtime_features.h"

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

void unescapeInPath(std::string& path, absl::string_view escape_sequence,
                    absl::string_view substitution) {
  std::vector<absl::string_view> split = absl::StrSplit(path, escape_sequence);
  if (split.size() == 1) {
    return;
  }
  path = absl::StrJoin(split, substitution);
}

} // namespace

/* static */
bool PathUtil::canonicalPath(RequestHeaderMap& headers) {
  ASSERT(headers.Path());
  const auto original_path = headers.getPathValue();
  const absl::optional<std::string> normalized_path = PathTransformer::rfcNormalize(original_path);
  if (!normalized_path.has_value()) {
    return false;
  }
  headers.setPath(normalized_path.value());
  return true;
}

void PathUtil::mergeSlashes(RequestHeaderMap& headers) {
  ASSERT(headers.Path());
  const auto original_path = headers.getPathValue();
  const absl::optional<std::string> normalized =
      PathTransformer::mergeSlashes(original_path).value();
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

PathTransformer::PathTransformer(const bool should_normalize_path,
                                 const bool should_merge_slashes) {
  if (should_normalize_path) {
    transformations_.emplace_back(PathTransformer::rfcNormalize);
    normalize_path_actions_.push_back(NormalizePathAction::Continue);
  }
  if (should_merge_slashes) {
    transformations_.emplace_back(PathTransformer::mergeSlashes);
    normalize_path_actions_.push_back(NormalizePathAction::Continue);
  }
}

PathTransformer::PathTransformer(
    envoy::type::http::v3::PathTransformation const& path_transformation) {
  const auto& operations = path_transformation.operations();
  std::vector<uint64_t> operation_hashes;
  for (auto const& operation : operations) {
    uint64_t operation_hash = MessageUtil::hash(operation);
    if (find(operation_hashes.begin(), operation_hashes.end(), operation_hash) !=
        operation_hashes.end()) {
      // Currently we only have RFC normalization and merge slashes, don't expect duplicates for
      // these transformations.
      throw EnvoyException("Duplicate path transformation");
    }
    // The transformation to apply
    if (operation.has_normalize_path_rfc_3986()) {
      transformations_.emplace_back(PathTransformer::rfcNormalize);
    } else if (operation.has_merge_slashes()) {
      transformations_.emplace_back(PathTransformer::mergeSlashes);
    }
    // The action to be performed if the transformation changed the path.
    switch (operation.normalize_path_action()) {
    case envoy::type::http::v3::PathTransformation::CONTINUE:
      normalize_path_actions_.push_back(NormalizePathAction::Continue);
      break;
    case envoy::type::http::v3::PathTransformation::REDIRECT:
      normalize_path_actions_.push_back(NormalizePathAction::Redirect);
      break;
    case envoy::type::http::v3::PathTransformation::REJECT:
      normalize_path_actions_.push_back(NormalizePathAction::Reject);
      break;
    default:
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }
    operation_hashes.push_back(operation_hash);
  }
}
PathUtil::UnescapeSlashesResult PathUtil::unescapeSlashes(RequestHeaderMap& headers) {
  ASSERT(headers.Path());
  const auto original_path = headers.getPathValue();
  const auto original_length = original_path.length();
  // Only operate on path component in URL.
  const absl::string_view::size_type query_start = original_path.find('?');
  const absl::string_view path = original_path.substr(0, query_start);
  if (path.find('%') == absl::string_view::npos) {
    return UnescapeSlashesResult::NotFound;
  }
  const absl::string_view query = absl::ClippedSubstr(original_path, query_start);

  // TODO(yanavlasov): optimize this by adding case insensitive matcher
  std::string decoded_path{path};
  unescapeInPath(decoded_path, "%2F", "/");
  unescapeInPath(decoded_path, "%2f", "/");
  unescapeInPath(decoded_path, "%5C", "\\");
  unescapeInPath(decoded_path, "%5c", "\\");
  headers.setPath(absl::StrCat(decoded_path, query));
  // Path length will not match if there were unescaped %2f or %5c
  return headers.getPathValue().length() != original_length
             ? UnescapeSlashesResult::FoundAndUnescaped
             : UnescapeSlashesResult::NotFound;
}

absl::optional<std::string> PathTransformer::transform(absl::string_view original) const {
  absl::optional<std::string> path_string = std::string(original);
  absl::string_view path_string_view = original;
  ;
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
