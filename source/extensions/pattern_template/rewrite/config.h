#pragma once

#include "envoy/extensions/pattern_template/rewrite/v3/pattern_template_rewrite.pb.h"
#include "envoy/router/path_rewrite_policy.h"

#include "source/extensions/pattern_template/rewrite/pattern_template_rewrite.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace Rewrite {

class PatternTemplateRewritePredicateFactory : public Router::PathRewritePredicateFactory {
public:
  Router::PathRewritePredicateSharedPtr
  createPathRewritePredicate(std::string url_pattern, std::string url_rewrite_pattern) override {
    return std::make_shared<PatternTemplateRewritePredicate>(url_pattern, url_rewrite_pattern);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::pattern_template::rewrite::v3::PatternTemplateRewriteConfig>();
  }

  std::string name() const override { return "envoy.path_rewrite_policy.pattern_template_rewrite_predicate"; }
  std::string category() const override { return "envoy.path_rewrite_policy"; }
};

} // namespace Rewrite
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
