#include "source/extensions/http/stateful_session/header/header.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace Header {

void HeaderBasedSessionStateFactory::SessionStateImpl::onUpdate(
    const Upstream::HostDescription& host, Envoy::Http::ResponseHeaderMap& headers) {
  absl::string_view host_address = host.address()->asStringView();
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
