#pragma once

#include <string>

#include "envoy/extensions/path/match/uri_template/v3/uri_template_match.pb.h"
#include "envoy/router/path_matcher.h"

#include "source/extensions/path/uri_template_lib/uri_template.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "re2/re2.h"

namespace Envoy {
namespace Extensions {
namespace UriTemplate {
namespace Match {

const absl::string_view NAME = "envoy.path.match.uri_template.uri_template_matcher";

/**
 * UriTemplateMatcher allows matching based on uri templates.
 * Examples of several uri templates types are below:
 * Variable: /foo/bar/{var}
 *    Will match any path that starts with /foo/bar and has one additional segment
 * Literal: /foo/bar/goo
 *    Will match only a path of /foo/bar/goo
 */
class UriTemplateMatcher : public Router::PathMatcher {
public:
  explicit UriTemplateMatcher(
      const envoy::extensions::path::match::uri_template::v3::UriTemplateMatchConfig& config)
      : path_template_(config.path_template()),
        matching_pattern_regex_(convertPathPatternSyntaxToRegex(path_template_).value()) {}

  // Router::PathMatcher
  bool match(absl::string_view path) const override;
  absl::string_view uriTemplate() const override;
  absl::string_view name() const override { return NAME; }

private:
  const std::string path_template_;
  const RE2 matching_pattern_regex_;
};

} // namespace Match
} // namespace UriTemplate
} // namespace Extensions
} // namespace Envoy
