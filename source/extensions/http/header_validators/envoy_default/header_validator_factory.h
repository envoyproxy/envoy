#pragma once

#include "envoy/extensions/http/header_validators/envoy_default/v3/header_validator.pb.h"
#include "envoy/http/header_validator.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

class HeaderValidatorFactory : public ::Envoy::Http::HeaderValidatorFactory {
public:
  HeaderValidatorFactory(
      const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
          config,
      Server::Configuration::ServerFactoryContext& server_context);

  ::Envoy::Http::ServerHeaderValidatorPtr
  createServerHeaderValidator(::Envoy::Http::Protocol protocol,
                              ::Envoy::Http::HeaderValidatorStats& stats) override;

  ::Envoy::Http::ClientHeaderValidatorPtr
  createClientHeaderValidator(::Envoy::Http::Protocol protocol,
                              ::Envoy::Http::HeaderValidatorStats& stats) override;

  const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
  getConfigurationForTestsOnly() const {
    return config_;
  }

private:
  const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
      config_;
  Server::Configuration::ServerFactoryContext& server_context_;
};

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
