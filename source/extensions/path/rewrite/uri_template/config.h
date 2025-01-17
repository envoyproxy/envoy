#pragma once

#include "envoy/extensions/path/rewrite/uri_template/v3/uri_template_rewrite.pb.h"
#include "envoy/extensions/path/rewrite/uri_template/v3/uri_template_rewrite.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/router/path_rewriter.h"

#include "source/extensions/path/rewrite/uri_template/uri_template_rewrite.h"

namespace Envoy {
namespace Extensions {
namespace UriTemplate {
namespace Rewrite {

class UriTemplateRewriterFactory : public Router::PathRewriterFactory {
public:
  absl::StatusOr<Router::PathRewriterSharedPtr>
  createPathRewriter(const Protobuf::Message& rewrite_config) override {
    auto path_rewrite_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::path::rewrite::uri_template::v3::UriTemplateRewriteConfig&>(
        rewrite_config, ProtobufMessage::getStrictValidationVisitor());

    if (!UriTemplate::isValidRewritePattern(path_rewrite_config.path_template_rewrite()).ok()) {
      return absl::InvalidArgumentError(
          fmt::format("path_rewrite_policy.path_template_rewrite {} is invalid",
                      path_rewrite_config.path_template_rewrite()));
    }
    return std::make_shared<UriTemplateRewriter>(path_rewrite_config);
  }

  // Router::PathRewriterFactory
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::path::rewrite::uri_template::v3::UriTemplateRewriteConfig>();
  }

  std::string name() const override {
    return "envoy.path.rewrite.uri_template.uri_template_rewriter";
  }
};

DECLARE_FACTORY(UriTemplateRewriterFactory);

} // namespace Rewrite
} // namespace UriTemplate
} // namespace Extensions
} // namespace Envoy
