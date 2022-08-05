#ifndef SOURCE_EXTENSIONS_PATH_REWRITE_PATTERN_TEMPLATE_PATTERN_TEMPLATE_H
#define SOURCE_EXTENSIONS_PATH_REWRITE_PATTERN_TEMPLATE_PATTERN_TEMPLATE_H

#include <string>

#include "envoy/extensions/path/match/pattern_template/v3/pattern_template_match.pb.h"
#include "envoy/extensions/path/match/pattern_template/v3/pattern_template_match.pb.validate.h"
#include "envoy/extensions/path/rewrite/pattern_template/v3/pattern_template_rewrite.pb.h"
#include "envoy/extensions/path/rewrite/pattern_template/v3/pattern_template_rewrite.pb.validate.h"
#include "envoy/router/path_rewrite_policy.h"

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
    "envoy.path.rewrite.pattern_template.pattern_template_rewrite_predicate";

class PatternTemplateRewritePredicate : public Router::PathRewritePredicate {
public:
  explicit PatternTemplateRewritePredicate(
      const envoy::extensions::path::rewrite::pattern_template::v3::PatternTemplateRewriteConfig&
          rewrite_config)
      : url_rewrite_pattern_(rewrite_config.path_template_rewrite()) {}

  std::string pattern() const override { return url_rewrite_pattern_; }

  absl::StatusOr<std::string> rewriteUrl(absl::string_view current_pattern,
                                         absl::string_view matched_path) const override;

  static absl::Status isValidRewritePattern(std::string match_pattern, std::string rewrite_pattern);

  absl::string_view name() const override { return NAME; }

private:
  // Returns the rewritten URL path based on the given parsed rewrite pattern.
  // Used for template-based URL rewrite.
  absl::StatusOr<std::string> rewriteURLTemplatePattern(
      absl::string_view url, absl::string_view capture_regex,
      const envoy::extensions::pattern_template::PatternTemplateRewriteSegments& rewrite_pattern)
      const;

  std::string url_rewrite_pattern_{nullptr};
};

} // namespace Rewrite
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy

#endif // SOURCE_EXTENSIONS_PATH_REWRITE_PATTERN_TEMPLATE_PATTERN_TEMPLATE_H
