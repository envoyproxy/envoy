#include "source/extensions/http/stateful_session/header/header.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace Header {

bool HeaderBasedSessionStateFactory::SessionStateImpl::onUpdate(
    absl::string_view host_address, Envoy::Http::ResponseHeaderMap& headers) {
  const bool host_changed =
      !upstream_address_.has_value() || host_address != upstream_address_.value();
  if (host_changed) {
    const std::string encoded_address =
        Envoy::Base64::encode(host_address.data(), host_address.length());
    headers.setCopy(factory_.getHeaderName(), encoded_address);
  }
  return host_changed;
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
