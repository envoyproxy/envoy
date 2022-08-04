#pragma once

#include "envoy/extensions/path/rewrite/pattern_template/v3/pattern_template_rewrite.pb.h"
#include "envoy/extensions/path/rewrite/pattern_template/v3/pattern_template_rewrite.pb.validate.h"
#include "envoy/router/path_rewrite_policy.h"

#include "source/extensions/path/rewrite/pattern_template/pattern_template_rewrite.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace Rewrite {

class PatternTemplateRewritePredicateFactory : public Router::PathRewritePredicateFactory {
public:
  absl::StatusOr<Router::PathRewritePredicateSharedPtr>
  createPathRewritePredicate(const Protobuf::Message& rewrite_config) override {
    auto path_rewrite_config =
        MessageUtil::downcastAndValidate<const envoy::extensions::path::rewrite::pattern_template::
                                             v3::PatternTemplateRewriteConfig&>(
            rewrite_config, ProtobufMessage::getStrictValidationVisitor());

    if (!PatternTemplate::isValidPathTemplateRewritePattern(
             path_rewrite_config.path_template_rewrite())
             .ok()) {
      return absl::InvalidArgumentError(
          fmt::format("path_rewrite_policy.path_template_rewrite {} is invalid",
                      path_rewrite_config.path_template_rewrite()));
    }
    return std::make_shared<PatternTemplateRewritePredicate>(path_rewrite_config);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::path::rewrite::pattern_template::v3::PatternTemplateRewriteConfig>();
  }

  std::string name() const override {
    return "envoy.path.rewrite.pattern_template.pattern_template_rewrite_predicate";
  }
  std::string category() const override { return "envoy.path.rewrite"; }
};

} // namespace Rewrite
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
