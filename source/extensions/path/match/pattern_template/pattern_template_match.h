#ifndef SOURCE_EXTENSIONS_PATH_MATCH_PATTERN_TEMPLATE_PATTERN_TEMPLATE_MATCH_H
#define SOURCE_EXTENSIONS_PATH_MATCH_PATTERN_TEMPLATE_PATTERN_TEMPLATE_MATCH_H

#include <string>

#include "envoy/extensions/path/match/v3/pattern_template_match.pb.h"
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
    explicit PatternTemplateMatchPredicate(const Protobuf::Message&, std::string url_pattern)
      : matching_pattern_regex_(RE2(convertURLPatternSyntaxToRegex(url_pattern).value())) {}

    absl::string_view name() const override {
      return "envoy.pattern_template.pattern_template_match_predicate";
    }

    bool match(absl::string_view pattern) const override;

  private:
    RE2 matching_pattern_regex_{nullptr};
};

} // namespace Match
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy

#endif // SOURCE_EXTENSIONS_PATH_MATCH_PATTERN_TEMPLATE_PATTERN_TEMPLATE_MATCH_H
