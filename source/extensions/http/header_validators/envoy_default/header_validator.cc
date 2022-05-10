#include "source/extensions/http/header_validators/envoy_default/header_validator.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

using ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig;
using ::Envoy::Http::HeaderString;

HeaderValidatorFactory::HeaderValidatorFactory(const HeaderValidatorConfig& config)
    : config_(config) {}

::Envoy::Http::HeaderValidatorPtr
HeaderValidatorFactory::create(::Envoy::Http::HeaderValidatorFactory::Protocol protocol) {
  return std::make_unique<HeaderValidator>(config_, protocol);
}

HeaderValidator::HeaderValidator(const HeaderValidatorConfig&,
                                 ::Envoy::Http::HeaderValidatorFactory::Protocol) {}

::Envoy::Http::HeaderValidator::HeaderEntryValidationResult
HeaderValidator::validateHeaderEntry(const HeaderString&, const HeaderString&) {
  return ::Envoy::Http::HeaderValidator::HeaderEntryValidationResult::Accept;
}

::Envoy::Http::HeaderValidator::RequestHeaderMapValidationResult
HeaderValidator::validateRequestHeaderMap(::Envoy::Http::RequestHeaderMap&) {
  return ::Envoy::Http::HeaderValidator::RequestHeaderMapValidationResult::Accept;
}

::Envoy::Http::HeaderValidator::ResponseHeaderMapValidationResult
HeaderValidator::validateResponseHeaderMap(::Envoy::Http::ResponseHeaderMap&) {
  return ::Envoy::Http::HeaderValidator::ResponseHeaderMapValidationResult::Accept;
}

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
