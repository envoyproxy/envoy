#pragma once

#include "envoy/extensions/path/rewrite/pattern_template/v3/pattern_template_rewrite.pb.h"
#include "envoy/extensions/path/rewrite/pattern_template/v3/pattern_template_rewrite.pb.validate.h"
#include "envoy/router/path_rewrite_policy.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace Rewrite {

class PatternTemplateRewritePredicateFactory : public Router::PathRewritePredicateFactory {
public:
  Router::PathRewritePredicateSharedPtr
  createPathRewritePredicate(const Protobuf::Message& rewrite_config) override {
    // TODO(silverstar195): Implement createPathRewritePredicate
    return nullptr;
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::path::rewrite::pattern_template::v3::PatternTemplateRewriteConfig>();
  }

  std::string name() const override {
    return "envoy.path.rewrite.pattern_template.v3.pattern_template_rewrite_predicate";
  }

  std::string category() const override { return "envoy.path.rewrite"; }
};

} // namespace Rewrite
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
