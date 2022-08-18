#pragma once

#include <string>

#include "envoy/extensions/path/match/pattern_template/v3/pattern_template_match.pb.h"
#include "envoy/extensions/path/match/pattern_template/v3/pattern_template_match.pb.validate.h"
#include "envoy/extensions/path/rewrite/pattern_template/v3/pattern_template_rewrite.pb.h"
#include "envoy/extensions/path/rewrite/pattern_template/v3/pattern_template_rewrite.pb.validate.h"
#include "envoy/router/path_match.h"
#include "envoy/router/path_rewrite.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/path/pattern_template_lib/pattern_template.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace Rewrite {

const absl::string_view NAME =
    "envoy.path.rewrite.pattern_template.pattern_template_rewriter";

/**
 * PatternTemplateRewriter allows rewriting paths based on match pattern variables provided
 * in PatternTemplateMatcher.
 *
 * Example:
 * PatternTemplateMatcher = /foo/bar/{var}
 * PatternTemplateRewriter = /foo/{var}
 *    Will replace segment of path with value of {var}
 *    e.g. /foo/bar/cat -> /foo/cat
 */
class PatternTemplateRewriter : public Router::PathRewriter {
public:
  explicit PatternTemplateRewriter(
      const envoy::extensions::path::rewrite::pattern_template::v3::PatternTemplateRewriteConfig&
          rewrite_config)
      : url_rewrite_pattern_(rewrite_config.path_template_rewrite()) {}

  // Router::PathRewriter
  absl::string_view pattern() const override { return url_rewrite_pattern_; }

  absl::StatusOr<std::string> rewriteUrl(absl::string_view pattern,
                                         absl::string_view matched_path) const override;

  absl::Status isCompatibleMatchPolicy(Router::PathMatcherSharedPtr match_policy,
                                       bool active_policy) const override;

  absl::string_view name() const override { return NAME; }

private:
  std::string url_rewrite_pattern_{nullptr};
};

} // namespace Rewrite
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
