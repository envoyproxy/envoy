#include "source/extensions/http/stateful_session/header/header.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace Header {

void HeaderBasedSessionStateFactory::SessionStateImpl::onUpdate(
    absl::string_view host_address, Envoy::Http::ResponseHeaderMap& headers) {
  if (!upstream_address_.has_value() || host_address != upstream_address_.value()) {
    const std::string encoded_address =
        Envoy::Base64::encode(host_address.data(), host_address.length());
    headers.setCopy(factory_.getHeaderName(), encoded_address);
  }
}

HeaderBasedSessionStateFactory::HeaderBasedSessionStateFactory(
    const HeaderBasedSessionStateProto& config)
    : name_(config.name()) {
  if (config.name().empty()) {
    throw EnvoyException("Header name cannot be empty for header based stateful sessions");
  }
}

} // namespace Header
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
