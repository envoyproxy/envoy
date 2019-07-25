#include "common/http/path_utility.h"

#include "common/chromium_url/url_canon.h"
#include "common/chromium_url/url_canon_stdstring.h"
#include "common/common/logger.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Http {

namespace {
absl::optional<std::string> canonicalizePath(absl::string_view original_path) {
  std::string canonical_path;
  chromium_url::Component in_component(0, original_path.size());
  chromium_url::Component out_component;
  chromium_url::StdStringCanonOutput output(&canonical_path);
  if (!chromium_url::CanonicalizePath(original_path.data(), in_component, &output,
                                      &out_component)) {
    return absl::nullopt;
  } else {
    output.Complete();
    return absl::make_optional(std::move(canonical_path));
  }
}
} // namespace

/* static */
bool PathUtil::canonicalPath(HeaderEntry& path_header) {
  const auto original_path = path_header.value().getStringView();
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
  path_header.value(normalized_path);
  return true;
}

void PathUtil::mergeSlashes(HeaderEntry& path_header) {
  const auto original_path = path_header.value().getStringView();
  // Only operate on path component in URL.
  const size_t query_start = original_path.find('?');
  const auto path = original_path.substr(0, query_start);
  const auto query = absl::ClippedSubstr(original_path, query_start);
  if (path.find("//") == absl::string_view::npos) {
    return;
  }

  std::string simplified_path;
  simplified_path.reserve(original_path.size());
  for (size_t i = 0; i < path.size(); ++i) {
    if (i > 0 && path[i] == '/' && path[i - 1] == '/') {
      continue;
    }
    simplified_path.push_back(path[i]);
  }
  simplified_path.insert(simplified_path.end(), query.begin(), query.end());
  path_header.value(simplified_path);
}

} // namespace Http
} // namespace Envoy
