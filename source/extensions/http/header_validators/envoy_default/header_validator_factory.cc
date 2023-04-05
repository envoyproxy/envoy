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

HeaderValidatorFactory::HeaderValidatorFactory(const HeaderValidatorConfig& config)
    : config_(config) {}

::Envoy::Http::HeaderValidatorPtr
HeaderValidatorFactory::createServerHeaderValidator(Protocol protocol,
                                                    ::Envoy::Http::HeaderValidatorStats& stats) {
  switch (protocol) {
  case Protocol::Http3:
  case Protocol::Http2:
    return std::make_unique<ServerHttp2HeaderValidator>(config_, protocol, stats);
  case Protocol::Http11:
  case Protocol::Http10:
    return std::make_unique<ServerHttp1HeaderValidator>(config_, protocol, stats);
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

::Envoy::Http::ClientHeaderValidatorPtr
HeaderValidatorFactory::createClientHeaderValidator(Protocol protocol,
                                                    ::Envoy::Http::HeaderValidatorStats& stats) {
  switch (protocol) {
  case Protocol::Http3:
  case Protocol::Http2:
    return std::make_unique<ClientHttp2HeaderValidator>(config_, protocol, stats);
  case Protocol::Http11:
  case Protocol::Http10:
    return std::make_unique<ClientHttp1HeaderValidator>(config_, protocol, stats);
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
