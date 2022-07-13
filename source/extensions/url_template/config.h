#pragma once

#include "envoy/extensions/url_template/v3/route_url_rewrite_pattern.pb.h"
#include "envoy/router/url_template.h"

#include "source/extensions/url_template/url_template_matching.h"

namespace Envoy {
namespace matching {

class PatternTemplatePredicateFactory : public Router::PatternTemplatePredicateFactory {
public:
  Router::PatternTemplatePredicateSharedPtr
  createUrlTemplatePredicate(std::string url_pattern, std::string url_rewrite_pattern) override {
    return std::make_shared<matching::PatternTemplatePredicate>(url_pattern, url_rewrite_pattern);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // may not be used to investigate
    return std::make_unique<envoy::extensions::url_template::v3::RouteUrlRewritePattern>();
  }

 std::string name() const override {
    return "envoy.url_template.pattern_template_predicates";
  }

};

} // namespace matching
} // namespace Envoy