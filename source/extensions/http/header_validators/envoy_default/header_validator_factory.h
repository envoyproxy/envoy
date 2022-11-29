#pragma once

#include "envoy/extensions/http/header_validators/envoy_default/v3/header_validator.pb.h"
#include "envoy/http/header_validator.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

class HeaderValidatorFactory : public ::Envoy::Http::HeaderValidatorFactory {
public:
  HeaderValidatorFactory(
      const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
          config);

  ::Envoy::Http::HeaderValidatorPtr create(::Envoy::Http::Protocol protocol,
                                           StreamInfo::StreamInfo& stream_info) override;

private:
  const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
      config_;
};

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
