#ifndef SOURCE_EXTENSIONS_PATTERN_TEMPLATE_PATTERN_TEMPLATE_MATCH_H
#define SOURCE_EXTENSIONS_PATTERN_TEMPLATE_PATTERN_TEMPLATE_MATCH_H

#include <string>

#include "envoy/extensions/pattern_template/match/v3/pattern_template_match.pb.h"
#include "envoy/router/pattern_template.h"

#include "source/extensions/pattern_template/pattern_template.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace Match {

class PatternTemplateMatchPredicate : public Router::PatternTemplateMatchPredicate {
public:
    explicit PatternTemplateMatchPredicate(std::string url_pattern)
      : matching_pattern_regex_(RE2(convertURLPatternSyntaxToRegex(url_pattern).value())) {}

    PatternTemplateMatchPredicate(){};
    ~PatternTemplateMatchPredicate(){};

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

#endif // SOURCE_EXTENSIONS_PATTERN_TEMPLATE_PATTERN_TEMPLATE_MATCH_H
