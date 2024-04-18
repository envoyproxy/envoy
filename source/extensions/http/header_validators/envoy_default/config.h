#pragma once

#include "envoy/http/header_validator_factory.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

/**
 * Config registration for the custom header IP detection extension.
 * @see OriginalIPDetectionFactory.
 */
class HeaderValidatorFactoryConfig : public ::Envoy::Http::HeaderValidatorFactoryConfig {
public:
  ::Envoy::Http::HeaderValidatorFactoryPtr
  createFromProto(const Protobuf::Message& config,
                  Server::Configuration::ServerFactoryContext& server_context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override { return "envoy.http.header_validators.envoy_default"; }
};

DECLARE_FACTORY(HeaderValidatorFactoryConfig);

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
