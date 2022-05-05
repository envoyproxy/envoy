#include "source/extensions/http/unified_header_validators/envoy_default/unified_header_validator.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace UnifiedHeaderValidators {
namespace EnvoyDefault {

using ::envoy::extensions::http::unified_header_validators::envoy_default::v3::
    UnifiedHeaderValidatorConfig;
using ::Envoy::Http::HeaderString;

UnifiedHeaderValidatorFactory::UnifiedHeaderValidatorFactory(
    const UnifiedHeaderValidatorConfig& config)
    : config_(config) {}

::Envoy::Http::UnifiedHeaderValidatorPtr UnifiedHeaderValidatorFactory::create(
    ::Envoy::Http::UnifiedHeaderValidatorFactory::Protocol protocol) {
  return std::make_unique<UnifiedHeaderValidator>(config_, protocol);
}

UnifiedHeaderValidator::UnifiedHeaderValidator(
    const UnifiedHeaderValidatorConfig&, ::Envoy::Http::UnifiedHeaderValidatorFactory::Protocol) {}

::Envoy::Http::UnifiedHeaderValidator::HeaderEntryValidationResult
UnifiedHeaderValidator::validateHeaderEntry(const HeaderString&, const HeaderString&) {
  return ::Envoy::Http::UnifiedHeaderValidator::HeaderEntryValidationResult::Accept;
}

::Envoy::Http::UnifiedHeaderValidator::RequestHeaderMapValidationResult
UnifiedHeaderValidator::validateRequestHeaderMap(::Envoy::Http::RequestHeaderMap&) {
  return ::Envoy::Http::UnifiedHeaderValidator::RequestHeaderMapValidationResult::Accept;
}

::Envoy::Http::UnifiedHeaderValidator::ResponseHeaderMapValidationResult
UnifiedHeaderValidator::validateResponseHeaderMap(::Envoy::Http::ResponseHeaderMap&) {
  return ::Envoy::Http::UnifiedHeaderValidator::ResponseHeaderMapValidationResult::Accept;
}

} // namespace EnvoyDefault
} // namespace UnifiedHeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
