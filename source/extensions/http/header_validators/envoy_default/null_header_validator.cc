#include "source/extensions/http/header_validators/envoy_default/null_header_validator.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

using ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig;
using ::Envoy::Http::HeaderString;
using ::Envoy::Http::Protocol;

NullHeaderValidator::NullHeaderValidator(const HeaderValidatorConfig&, Protocol,
                                         StreamInfo::StreamInfo&) {}

::Envoy::Http::HeaderValidator::HeaderEntryValidationResult
NullHeaderValidator::validateRequestHeaderEntry(const HeaderString&, const HeaderString&) {
  return HeaderEntryValidationResult::success();
}

::Envoy::Http::HeaderValidator::HeaderEntryValidationResult
NullHeaderValidator::validateResponseHeaderEntry(const HeaderString&, const HeaderString&) {
  return HeaderEntryValidationResult::success();
}

::Envoy::Http::HeaderValidator::RequestHeaderMapValidationResult
NullHeaderValidator::validateRequestHeaderMap(::Envoy::Http::RequestHeaderMap&) {
  return RequestHeaderMapValidationResult::success();
}

::Envoy::Http::HeaderValidator::ResponseHeaderMapValidationResult
NullHeaderValidator::validateResponseHeaderMap(::Envoy::Http::ResponseHeaderMap&) {
  return ResponseHeaderMapValidationResult::success();
}

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
