#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "source/common/common/regex.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Router {

// Path rewriting helpers shared by the native route rewrites and by extensions that build routes.
// They never assert or throw on a rejected input, so a caller on the request path can turn a
// rejected rewrite into a failure rather than an invariant violation or an oversized allocation.

// Replaces the path_to_strip prefix of origin_path with new_path_to_replace. Returns nullopt when
// path_to_strip is longer than origin_path or when the result would exceed max_bytes, and never
// reserves before that bound is known to hold.
std::optional<std::string> generateNewPath(absl::string_view origin_path,
                                           absl::string_view path_to_strip,
                                           absl::string_view new_path_to_replace, size_t max_bytes);

// Rewrites path by a prefix or a regex. matched must be a case insensitive prefix of path for a
// prefix rewrite. Returns nullopt when matched is not such a prefix, when the regex produces an
// empty result, when neither rewrite is set, or when the result would exceed max_bytes.
std::optional<std::string> rewritePathByPrefixOrRegex(absl::string_view path,
                                                      absl::string_view matched,
                                                      absl::string_view prefix_rewrite,
                                                      const Regex::CompiledMatcher* regex_rewrite,
                                                      absl::string_view regex_rewrite_substitution,
                                                      size_t max_bytes);

} // namespace Router
} // namespace Envoy
