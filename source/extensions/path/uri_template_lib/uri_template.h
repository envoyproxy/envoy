#pragma once

#include <ostream>
#include <string>

#include "source/extensions/path/uri_template_lib/uri_template_internal.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace UriTemplate {

// This library provides functionality to rewrite paths based on templates
// (https://www.rfc-editor.org/rfc/rfc6570.html). Paths are matches against "match patterns" which
// capture matched tokens. For example: The match pattern "/foo/{bar}" will match "/foo/cat" and
// will capture "cat" in the variable "bar".
//
// A matched path can then be rewritten with a rewrite pattern.
// For example: rewrite pattern "/pat/hat/{bar}" would use the captured variable "bar" from above
// to rewrite "/foo/cat" into "/pat/hat/cat"

enum class RewriteStringKind { Variable, Literal };

struct ParsedSegment {
  ParsedSegment(absl::string_view value, RewriteStringKind kind) : value_(value), kind_(kind) {}
  absl::string_view value_;
  RewriteStringKind kind_;

  friend std::ostream& operator<<(std::ostream& os, const ParsedSegment& parsed_segment);
};

// Stores string literals and regex capture indexes for rewriting paths
using RewriteSegment = absl::variant<int, std::string>;

// Stores all segments in left to right order for a path rewrite
using RewriteSegments = std::vector<RewriteSegment>;

/**
 * Returns the safe regex that Envoy understands that is equivalent to the given pattern.
 */
absl::StatusOr<std::string> convertPathPatternSyntaxToRegex(absl::string_view path_pattern);

/**
 * Parses the specified pattern into a sequence of segments (which are
 * either literals or variable names).
 **/
absl::StatusOr<std::vector<ParsedSegment>> parseRewritePattern(absl::string_view path_pattern);

/**
 * Returns the parsed path rewrite pattern and processes variables.
 */
absl::StatusOr<RewriteSegments> parseRewritePattern(absl::string_view pattern,
                                                    absl::string_view capture_regex);

/**
 * Returns true if provided rewrite pattern is valid.
 */
absl::Status isValidRewritePattern(absl::string_view path_template_rewrite);

/**
 * Returns true if path_template_rewrite and capture_regex have valid variables.
 * Every variable in rewrite MUST be present in match.
 * For example:
 * Match: /foo/{bar}/{var} and Rewrite: /goo/{var} is valid.
 * Match: /foo/{bar} and Rewrite: /goo/{bar}/{var} is invalid. Match is missing {var}.
 */
absl::Status isValidSharedVariableSet(absl::string_view pattern, absl::string_view capture_regex);

/**
 * Returns true if the match_pattern is valid.
 * Validation attempts to parse pattern into literals and variables.
 */
absl::Status isValidMatchPattern(absl::string_view match_pattern);

} // namespace UriTemplate
} // namespace Extensions
} // namespace Envoy
