#pragma once

#include "envoy/extensions/path/rewrite/pattern_template/v3/pattern_template_rewrite.pb.h"
#include "envoy/extensions/path/rewrite/pattern_template/v3/pattern_template_rewrite.pb.validate.h"
#include "envoy/router/path_rewriter.h"

#include "source/extensions/path/rewrite/pattern_template/pattern_template_rewrite.h"

namespace Envoy {
namespace Extensions {
namespace UriTemplate {
namespace Rewrite {

class PatternTemplateRewriterFactory : public Router::PathRewriterFactory {
public:
  absl::StatusOr<Router::PathRewriterSharedPtr>
  createPathRewriter(const Protobuf::Message& rewrite_config) override {
    auto path_rewrite_config =
        MessageUtil::downcastAndValidate<const envoy::extensions::path::rewrite::pattern_template::
                                             v3::PatternTemplateRewriteConfig&>(
            rewrite_config, ProtobufMessage::getStrictValidationVisitor());

    if (!UriTemplate::isValidRewritePattern(path_rewrite_config.path_template_rewrite()).ok()) {
      return absl::InvalidArgumentError(
          fmt::format("path_rewrite_policy.path_template_rewrite {} is invalid",
                      path_rewrite_config.path_template_rewrite()));
    }
    return std::make_shared<PatternTemplateRewriter>(path_rewrite_config);
  }

  // Router::PathRewriterFactory
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::path::rewrite::pattern_template::v3::PatternTemplateRewriteConfig>();
  }

  std::string name() const override {
    return "envoy.path.rewrite.pattern_template.pattern_template_rewriter";
  }
};

} // namespace Rewrite
} // namespace UriTemplate
} // namespace Extensions
} // namespace Envoy
