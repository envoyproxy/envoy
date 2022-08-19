#pragma once

#include <string>

#include "source/extensions/path/uri_template_lib/proto/uri_template_rewrite_segments.pb.h"
#include "source/extensions/path/uri_template_lib/uri_template_internal.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace UriTemplate {

enum class RewriteStringKind { Variable, Literal };

struct RewritePatternSegment {
  RewritePatternSegment(absl::string_view value, RewriteStringKind kind)
      : value_(value), kind_(kind) {}
  absl::string_view value_;
  RewriteStringKind kind_;
};

// Returns the safe regex that Envoy understands that is equivalent to the given pattern.
absl::StatusOr<std::string> convertURLPatternSyntaxToRegex(absl::string_view url_pattern);

// Parses the specified pattern into a sequence of segments (which are
// either literals or variable names).
absl::StatusOr<std::vector<RewritePatternSegment>>
parseRewritePattern(absl::string_view url_pattern);

// Returns the parsed Url rewrite pattern and processes variables.
absl::StatusOr<envoy::extensions::uri_template::UriTemplateRewriteSegments>
parseRewritePattern(absl::string_view pattern, absl::string_view capture_regex);

// Returns if provided rewrite pattern is valid
absl::Status isValidRewritePattern(absl::string_view path_template_rewrite);

// Returns if path_template_rewrite and capture_regex have valid variables.
// Every variable in rewrite MUST be present in match.
// For example:
// Match: /foo/{bar}/{var} and Rewrite: /goo/{var} is valid.
// Match: /foo/{bar} and Rewrite: /goo/{bar}/{var} is invalid. Match is missing {var}.
absl::Status isValidSharedVariableSet(absl::string_view pattern, absl::string_view capture_regex);

// Returns is the match_pattern is valid.
// Validation attempts to parse pattern into literals and variables.
absl::Status isValidMatchPattern(absl::string_view match_pattern);

// Concatenates literals and extracts variable values to form the final rewritten url.
// For example:
// rewrite_pattern: [var_index=2, literal="cat"]
// url: "/bar/var"
// capture_regex: "(1)/(2)"
// Rewrite would result in rewrite of "/var/cat".
absl::StatusOr<std::string> rewriteURLTemplatePattern(
    absl::string_view url, absl::string_view capture_regex,
    const envoy::extensions::uri_template::UriTemplateRewriteSegments& rewrite_pattern);

} // namespace UriTemplate
} // namespace Extensions
} // namespace Envoy
