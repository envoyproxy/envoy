#pragma once

#include <string>

#include "envoy/extensions/path/match/pattern_template/v3/pattern_template_match.pb.h"
#include "envoy/router/path_matcher.h"

#include "source/extensions/path/uri_template_lib/uri_template.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace UriTemplate {
namespace Match {

const absl::string_view NAME = "envoy.path.match.pattern_template.pattern_template_matcher";

/**
 * PatternTemplateMatcher allows matching based on pattern templates.
 * Examples of several pattern templates types are below:
 * Variable: /foo/bar/{var}
 *    Will match any path that starts with /foo/bar and has one additional segment
 * Literal: /foo/bar/goo
 *    Will match only a path of /foo/bar/goo
 */
class PatternTemplateMatcher : public Router::PathMatcher {
public:
  explicit PatternTemplateMatcher(
      const envoy::extensions::path::match::pattern_template::v3::PatternTemplateMatchConfig&
          config)
      : path_template_(config.path_template()) {}

  // Router::PathMatcher
  bool match(absl::string_view path) const override;

  absl::string_view pattern() const override;

  absl::string_view name() const override { return NAME; }

private:
  const std::string path_template_;
};

} // namespace Match
} // namespace UriTemplate
} // namespace Extensions
} // namespace Envoy
