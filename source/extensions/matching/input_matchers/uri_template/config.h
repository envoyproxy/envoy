#pragma once

#include "envoy/extensions/path/match/uri_template/v3/uri_template_match.pb.h"
#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace UriTemplate {

class UriTemplateInputMatcherFactory : public Envoy::Matcher::InputMatcherFactory {
public:
  Envoy::Matcher::InputMatcherFactoryCb createInputMatcherFactoryCb(
      const Protobuf::Message& config,
      Server::Configuration::ServerFactoryContext& factory_context) override;

  std::string name() const override { return "envoy.matching.matchers.uri_template"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::path::match::uri_template::v3::UriTemplateMatchConfig>();
  }
};

} // namespace UriTemplate
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
