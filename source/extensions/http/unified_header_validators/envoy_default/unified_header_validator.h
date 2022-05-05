#pragma once

#include "envoy/extensions/http/unified_header_validators/envoy_default/v3/unified_header_validator.pb.h"
#include "envoy/http/unified_header_validator.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace UnifiedHeaderValidators {
namespace EnvoyDefault {

/**
 * Custom header IP detection extension.
 */
class UnifiedHeaderValidator : public ::Envoy::Http::UnifiedHeaderValidator {
public:
  UnifiedHeaderValidator(const envoy::extensions::http::unified_header_validators::envoy_default::
                             v3::UnifiedHeaderValidatorConfig& config,
                         ::Envoy::Http::UnifiedHeaderValidatorFactory::Protocol protocol);

  HeaderEntryValidationResult
  validateHeaderEntry(const ::Envoy::Http::HeaderString& key,
                      const ::Envoy::Http::HeaderString& value) override;

  RequestHeaderMapValidationResult
  validateRequestHeaderMap(::Envoy::Http::RequestHeaderMap& header_map) override;

  ResponseHeaderMapValidationResult
  validateResponseHeaderMap(::Envoy::Http::ResponseHeaderMap& header_map) override;
};

class UnifiedHeaderValidatorFactory : public ::Envoy::Http::UnifiedHeaderValidatorFactory {
public:
  UnifiedHeaderValidatorFactory(const envoy::extensions::http::unified_header_validators::
                                    envoy_default::v3::UnifiedHeaderValidatorConfig& config);

  ::Envoy::Http::UnifiedHeaderValidatorPtr
  create(::Envoy::Http::UnifiedHeaderValidatorFactory::Protocol protocol) override;

private:
  const envoy::extensions::http::unified_header_validators::envoy_default::v3::
      UnifiedHeaderValidatorConfig config_;
};

} // namespace EnvoyDefault
} // namespace UnifiedHeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
