#include "source/common/http/path_utility.h"

#include "source/common/common/logger.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/types/optional.h"
#include "url/url_canon.h"
#include "url/url_canon_stdstring.h"

namespace Envoy {
namespace Http {

namespace {
absl::optional<std::string> canonicalizePath(absl::string_view original_path) {
  std::string canonical_path;
  url::Component in_component(0, original_path.size());
  url::Component out_component;
  url::StdStringCanonOutput output(&canonical_path);
  if (!url::CanonicalizePath(original_path.data(), in_component, &output, &out_component)) {
    return absl::nullopt;
  }
  output.Complete();
  return absl::make_optional(std::move(canonical_path));
}
} // namespace

/* static */
bool PathUtil::canonicalPath(RequestHeaderMap& headers) {
  ASSERT(headers.Path());
  const auto original_path = headers.getPathValue();
  // canonicalPath is supposed to apply on path component in URL instead of :path header
  const auto query_pos = original_path.find('?');
  auto normalized_path_opt = canonicalizePath(
      query_pos == original_path.npos
          ? original_path
          : absl::string_view(original_path.data(), query_pos) // '?' is not included
  );

  if (!normalized_path_opt.has_value()) {
    return false;
  }
  auto& normalized_path = normalized_path_opt.value();
  const absl::string_view query_suffix =
      query_pos == original_path.npos
          ? absl::string_view{}
          : absl::string_view{original_path.data() + query_pos, original_path.size() - query_pos};
  if (!query_suffix.empty()) {
    normalized_path.insert(normalized_path.end(), query_suffix.begin(), query_suffix.end());
  }
  headers.setPath(normalized_path);
  return true;
}

void PathUtil::mergeSlashes(RequestHeaderMap& headers) {
  ASSERT(headers.Path());
  const auto original_path = headers.getPathValue();
  // Only operate on path component in URL.
  const absl::string_view::size_type query_start = original_path.find('?');
  const absl::string_view path = original_path.substr(0, query_start);
  const absl::string_view query = absl::ClippedSubstr(original_path, query_start);
  if (path.find("//") == absl::string_view::npos) {
    return;
  }
  const absl::string_view path_prefix = absl::StartsWith(path, "/") ? "/" : absl::string_view();
  const absl::string_view path_suffix = absl::EndsWith(path, "/") ? "/" : absl::string_view();
  headers.setPath(absl::StrCat(path_prefix,
                               absl::StrJoin(absl::StrSplit(path, '/', absl::SkipEmpty()), "/"),
                               path_suffix, query));
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

  static const std::vector<std::pair<absl::string_view, absl::string_view>> replacements{
      {"%2F", "/"},
      {"%2f", "/"},
      {"%5C", "\\"},
      {"%5c", "\\"},
  };
  headers.setPath(absl::StrCat(absl::StrReplaceAll(path, replacements), query));

  // Path length will not match if there were unescaped %2f or %5c
  return headers.getPathValue().length() != original_length
             ? UnescapeSlashesResult::FoundAndUnescaped
             : UnescapeSlashesResult::NotFound;
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

} // namespace Http
} // namespace Envoy
