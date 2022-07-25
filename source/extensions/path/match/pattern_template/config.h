#pragma once

#include "envoy/extensions/path/match/v3/pattern_template_match.pb.h"
#include "envoy/router/pattern_template.h"

#include "source/extensions/path/match/pattern_template/pattern_template_match.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace Match {

class PatternTemplateMatchPredicateFactory : public Router::PatternTemplateMatchPredicateFactory {
public:
  Router::PatternTemplateMatchPredicateSharedPtr
  createUrlTemplateMatchPredicate(std::string url_pattern) override {
    return std::make_shared<PatternTemplateMatchPredicate>(url_pattern);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::path::match::pattern_template::v3::PatternTemplateMatchConfig>();
  }

  std::string name() const override { return "envoy.path.match.pattern_template.pattern_template_match_predicate"; }
  std::string category() const override { return "envoy.path.match"; }
};

} // namespace Match
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
