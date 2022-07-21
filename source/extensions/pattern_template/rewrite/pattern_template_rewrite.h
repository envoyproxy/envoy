#ifndef SOURCE_EXTENSIONS_PATTERN_TEMPLATE_PATTERN_TEMPLATE_REWRITE_H
#define SOURCE_EXTENSIONS_PATTERN_TEMPLATE_PATTERN_TEMPLATE_REWRITE_H

#include <string>

#include "envoy/extensions/pattern_template/rewrite/v3/pattern_template_rewrite.pb.h"
#include "envoy/router/pattern_template.h"

#include "source/extensions/pattern_template/pattern_template.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace Rewrite {

class PatternTemplateRewritePredicate : public Router::PatternTemplateRewritePredicate {
public:
  explicit PatternTemplateRewritePredicate(std::string url_pattern, std::string url_rewrite_pattern)
      : matching_pattern_regex_(RE2(convertURLPatternSyntaxToRegex(url_pattern).value())),
        url_rewrite_pattern_(url_rewrite_pattern) {}

  absl::string_view name() const override {
    return "envoy.pattern_template.pattern_template_rewrite_predicate";
  }

  absl::StatusOr<std::string> rewritePattern(absl::string_view current_pattern,
                                             absl::string_view matched_path) const override;

  static absl::Status isValidRewritePattern(std::string match_pattern,
                                               std::string rewrite_pattern);

private:
  // Returns the rewritten URL path based on the given parsed rewrite pattern.
  // Used for template-based URL rewrite.
  absl::StatusOr<std::string> rewriteURLTemplatePattern(
      absl::string_view url, absl::string_view capture_regex,
      const envoy::extensions::pattern_template::rewrite::v3::PatternTemplateRewrite&
          rewrite_pattern) const;

  RE2 matching_pattern_regex_{nullptr};
  std::string url_rewrite_pattern_{nullptr};
};

} // namespace Rewrite
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy

#endif // SOURCE_EXTENSIONS_PATTERN_TEMPLATE_PATTERN_TEMPLATE_REWRITE_H
