#pragma once

#include "envoy/http/unified_header_validator.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace UnifiedHeaderValidators {
namespace EnvoyDefault {

/**
 * Config registration for the custom header IP detection extension.
 * @see OriginalIPDetectionFactory.
 */
class UnifiedHeaderValidatorFactoryConfig
    : public ::Envoy::Http::UnifiedHeaderValidatorFactoryConfig {
public:
  ::Envoy::Http::UnifiedHeaderValidatorFactorySharedPtr
  createFromProto(const Protobuf::Message& config,
                  Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override { return "envoy.http.unified_header_validators.envoy_default"; }
};

DECLARE_FACTORY(UnifiedHeaderValidatorFactoryConfig);

} // namespace EnvoyDefault
} // namespace UnifiedHeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
