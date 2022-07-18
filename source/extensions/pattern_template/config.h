#pragma once

#include "envoy/extensions/pattern_template/v3/pattern_template_rewrite.pb.h"
#include "envoy/router/pattern_template.h"

#include "source/extensions/pattern_template/pattern_template_matching.h"

namespace Envoy {
namespace PatternTemplate {

class PatternTemplatePredicateFactory : public Router::PatternTemplatePredicateFactory {
public:
  Router::PatternTemplatePredicateSharedPtr
  createUrlTemplatePredicate(std::string url_pattern, std::string url_rewrite_pattern) override {
    return std::make_shared<PatternTemplatePredicate>(url_pattern, url_rewrite_pattern);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::pattern_template::v3::PatternTemplateRewrite>();
  }

  std::string name() const override { return "envoy.pattern_template.pattern_template_predicate"; }
};

} // namespace PatternTemplate
} // namespace Envoy