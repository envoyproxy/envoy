#ifndef SOURCE_EXTENSIONS_PATH_MATCH_PATTERN_TEMPLATE_PATTERN_TEMPLATE_MATCH_H
#define SOURCE_EXTENSIONS_PATH_MATCH_PATTERN_TEMPLATE_PATTERN_TEMPLATE_MATCH_H

#include <string>

#include "envoy/extensions/path/match/pattern_template/v3/pattern_template_match.pb.h"
#include "envoy/router/path_match_policy.h"

#include "source/extensions/path/pattern_template_lib/pattern_template.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace Match {

class PatternTemplateMatchPredicate : public Router::PathMatchPredicate {
public:
  explicit PatternTemplateMatchPredicate(
      const envoy::extensions::path::match::pattern_template::v3::PatternTemplateMatchConfig&
          config)
      : path_template_(config.path_template()){}

  absl::string_view name() const override {
    return "envoy.path.match.pattern_template.v3.pattern_template_match_predicate";
  }

  bool match(absl::string_view pattern) const override;
  std::string pattern() const override;

private:
  const std::string path_template_;
};

} // namespace Match
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy

#endif // SOURCE_EXTENSIONS_PATH_MATCH_PATTERN_TEMPLATE_PATTERN_TEMPLATE_MATCH_H
