#pragma once

#include "envoy/extensions/pattern_template/match/v3/pattern_template_match.pb.h"
#include "envoy/router/path_match_policy.h"

#include "source/extensions/pattern_template/match/pattern_template_match.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace Match {

class PatternTemplateMatchPredicateFactory : public Router::PathMatchPredicateFactory {
public:
  Router::PathMatchPredicateSharedPtr
  createPathMatchPredicate(std::string url_pattern) override {
    return std::make_shared<PatternTemplateMatchPredicate>(url_pattern);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::pattern_template::match::v3::PatternTemplateMatchConfig>();
  }

  std::string name() const override { return "envoy.path_match_policy.pattern_template_match_predicate"; }
  std::string category() const override { return "envoy.path_match_policy"; }
};

} // namespace Match
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
