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
HeaderValidatorFactory::create(Protocol protocol, StreamInfo::StreamInfo& stream_info) {
  switch (protocol) {
  case Protocol::Http3:
  case Protocol::Http2:
    return std::make_unique<Http2HeaderValidator>(config_, protocol, stream_info);
  case Protocol::Http11:
  case Protocol::Http10:
    return std::make_unique<Http1HeaderValidator>(config_, protocol, stream_info);
  }

  RELEASE_ASSERT(false, fmt::format("Unexpected protocol: {}", static_cast<int>(protocol)));
}

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
