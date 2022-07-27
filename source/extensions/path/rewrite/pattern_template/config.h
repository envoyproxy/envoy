#pragma once

#include "envoy/extensions/path/rewrite/pattern_template/v3/pattern_template_rewrite.pb.h"
#include "envoy/router/path_rewrite_policy.h"

#include "source/extensions/path/rewrite/pattern_template/pattern_template_rewrite.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace Rewrite {

class PatternTemplateRewritePredicateFactory : public Router::PathRewritePredicateFactory {
public:
  Router::PathRewritePredicateSharedPtr
  createPathRewritePredicate(const Protobuf::Message&, std::string url_pattern) override {
    return std::make_shared<PatternTemplateRewritePredicate>(url_pattern, "");
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::path::rewrite::pattern_template::v3::PatternTemplateRewriteConfig>();
  }

  std::string name() const override { return "envoy.path_rewrite_policy.pattern_template_rewrite_predicate"; }
  std::string category() const override { return "envoy.path_rewrite_policy"; }
};

} // namespace Rewrite
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
