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
HeaderValidatorFactory::create(::Envoy::Http::Protocol protocol,
                               StreamInfo::StreamInfo& stream_info) {
  return std::make_unique<HeaderValidator>(config_, protocol, stream_info);
}

HeaderValidator::HeaderValidator(const HeaderValidatorConfig&, ::Envoy::Http::Protocol,
                                 StreamInfo::StreamInfo& stream_info)
    : stream_info_(stream_info) {}

::Envoy::Http::HeaderValidator::HeaderEntryValidationResult
HeaderValidator::validateRequestHeaderEntry(const HeaderString&, const HeaderString&) {
  return HeaderEntryValidationResult::success();
}

::Envoy::Http::HeaderValidator::HeaderEntryValidationResult
HeaderValidator::validateResponseHeaderEntry(const HeaderString&, const HeaderString&) {
  return HeaderEntryValidationResult::success();
}

::Envoy::Http::HeaderValidator::RequestHeaderMapValidationResult
HeaderValidator::validateRequestHeaderMap(::Envoy::Http::RequestHeaderMap&) {
  return RequestHeaderMapValidationResult::success();
}

::Envoy::Http::HeaderValidator::ResponseHeaderMapValidationResult
HeaderValidator::validateResponseHeaderMap(::Envoy::Http::ResponseHeaderMap&) {
  return ResponseHeaderMapValidationResult::success();
}

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
