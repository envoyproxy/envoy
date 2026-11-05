#include "source/common/router/path_rewrite_utility.h"

#include "source/common/http/path_utility.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Router {

std::optional<std::string> generateNewPath(absl::string_view origin_path,
                                           absl::string_view path_to_strip,
                                           absl::string_view new_path_to_replace,
                                           size_t max_bytes) {
  if (path_to_strip.size() > origin_path.size()) {
    return std::nullopt;
  }
  const size_t tail = origin_path.size() - path_to_strip.size();
  // Check the bound before reserving, without overflowing the addition.
  if (new_path_to_replace.size() > max_bytes || tail > max_bytes - new_path_to_replace.size()) {
    return std::nullopt;
  }
  std::string result;
  result.reserve(new_path_to_replace.size() + tail);
  result.append(new_path_to_replace);
  result.append(origin_path.substr(path_to_strip.size()));
  return result;
}

std::optional<std::string> rewritePathByPrefixOrRegex(absl::string_view path,
                                                      absl::string_view matched,
                                                      absl::string_view prefix_rewrite,
                                                      const Regex::CompiledMatcher* regex_rewrite,
                                                      absl::string_view regex_rewrite_substitution,
                                                      size_t max_bytes) {
  if (!prefix_rewrite.empty()) {
    if (!absl::StartsWithIgnoreCase(path, matched)) {
      return std::nullopt;
    }
    return generateNewPath(path, matched, prefix_rewrite, max_bytes);
  }

  if (regex_rewrite != nullptr) {
    const absl::string_view path_only = Http::PathUtil::removeQueryAndFragment(path);
    std::string new_path_only = regex_rewrite->replaceAll(path_only, regex_rewrite_substitution);
    // An empty result is a failed rewrite, and a result over the bound is rejected before it is
    // grown into the full path.
    if (new_path_only.empty() || new_path_only.size() > max_bytes) {
      return std::nullopt;
    }
    return generateNewPath(path, path_only, new_path_only, max_bytes);
  }
  return std::nullopt;
}

} // namespace Router
} // namespace Envoy
