#pragma once

#include "envoy/extensions/url_template/v3/route_url_rewrite_pattern.pb.h"
#include "envoy/router/url_template.h"

#include "source/extensions/url_template/url_template_matching.h"

namespace Envoy {
namespace Extensions {
namespace UrlTemplate {

class UrlTemplatePredicateFactory : public Router::UrlTemplatePredicateFactory {
public:
  Router::UrlTemplatePredicateSharedPtr
  createUrlTemplatePredicate(std::string url_pattern, std::string url_rewrite_pattern) override {
    return std::make_shared<matching::UrlTemplatePredicate>(url_pattern, url_rewrite_pattern);
  }

  std::string name() const override { return "envoy.url_template_predicates"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // may not be used to investigate
    return std::make_unique<envoy::extensions::url_template::v3::RouteUrlRewritePattern>();
  }
};

} // namespace UrlTemplate
} // namespace Extensions
} // namespace Envoy