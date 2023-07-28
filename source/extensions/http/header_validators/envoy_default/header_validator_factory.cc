#include "source/extensions/http/header_validators/envoy_default/header_validator_factory.h"

#include "source/extensions/http/header_validators/envoy_default/http1_header_validator.h"
#include "source/extensions/http/header_validators/envoy_default/http2_header_validator.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

using ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig;
using ::Envoy::Http::Protocol;

namespace {

HeaderValidatorConfig setHeaderValidatorConfigDefaults(const HeaderValidatorConfig& config) {
  HeaderValidatorConfig new_config(config);
  if (new_config.uri_path_normalization_options().path_with_escaped_slashes_action() ==
      HeaderValidatorConfig::UriPathNormalizationOptions::IMPLEMENTATION_SPECIFIC_DEFAULT) {
    new_config.mutable_uri_path_normalization_options()->set_path_with_escaped_slashes_action(
        HeaderValidatorConfig::UriPathNormalizationOptions::KEEP_UNCHANGED);
  }
  return new_config;
}

} // namespace

HeaderValidatorFactory::HeaderValidatorFactory(
    const HeaderValidatorConfig& config,
    Server::Configuration::ServerFactoryContext& server_context)
    : config_(setHeaderValidatorConfigDefaults(config)), server_context_(server_context) {}

::Envoy::Http::ServerHeaderValidatorPtr
HeaderValidatorFactory::createServerHeaderValidator(Protocol protocol,
                                                    ::Envoy::Http::HeaderValidatorStats& stats) {
  ConfigOverrides config_overrides(server_context_.runtime().snapshot());

  switch (protocol) {
  case Protocol::Http3:
  case Protocol::Http2:
    return std::make_unique<ServerHttp2HeaderValidator>(config_, protocol, stats, config_overrides);
  case Protocol::Http11:
  case Protocol::Http10:
    return std::make_unique<ServerHttp1HeaderValidator>(config_, protocol, stats, config_overrides);
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

::Envoy::Http::ClientHeaderValidatorPtr
HeaderValidatorFactory::createClientHeaderValidator(Protocol protocol,
                                                    ::Envoy::Http::HeaderValidatorStats& stats) {
  ConfigOverrides config_overrides(server_context_.runtime().snapshot());

  switch (protocol) {
  case Protocol::Http3:
  case Protocol::Http2:
    return std::make_unique<ClientHttp2HeaderValidator>(config_, protocol, stats, config_overrides);
  case Protocol::Http11:
  case Protocol::Http10:
    return std::make_unique<ClientHttp1HeaderValidator>(config_, protocol, stats, config_overrides);
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
