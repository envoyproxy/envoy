#pragma once

#include "envoy/extensions/path/match/uri_template/v3/uri_template_match.pb.h"
#include "envoy/extensions/path/match/uri_template/v3/uri_template_match.pb.validate.h"
#include "envoy/router/path_matcher.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/path/match/uri_template/uri_template_match.h"

namespace Envoy {
namespace Extensions {
namespace UriTemplate {
namespace Match {

class UriTemplateMatcherFactory : public Router::PathMatcherFactory {
public:
  absl::StatusOr<Router::PathMatcherSharedPtr>
  createPathMatcher(const Protobuf::Message& config) override {
    auto path_match_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::path::match::uri_template::v3::UriTemplateMatchConfig&>(
        config, ProtobufMessage::getStrictValidationVisitor());

    if (!UriTemplate::isValidMatchPattern(path_match_config.path_template()).ok()) {
      return absl::InvalidArgumentError(fmt::format("path_match_policy.path_template {} is invalid",
                                                    path_match_config.path_template()));
    }

    return std::make_shared<UriTemplateMatcher>(path_match_config);
  }

  // Router::PathMatcherFactory
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::path::match::uri_template::v3::UriTemplateMatchConfig>();
  }

  std::string name() const override { return "envoy.path.match.uri_template.uri_template_matcher"; }
};

} // namespace Match
} // namespace UriTemplate
} // namespace Extensions
} // namespace Envoy
