#include "source/extensions/http/stateful_session/cookie/cookie.h"

#include "source/common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace Cookie {

void CookieBasedSessionStateFactory::SessionStateImpl::onUpdate(
    const Upstream::HostDescription& host, Envoy::Http::ResponseHeaderMap& headers) {
  absl::string_view host_address = host.address()->asStringView();
  if (!upstream_address_.has_value() || host_address != upstream_address_.value()) {
    const std::string encoded_address =
        Envoy::Base64::encode(host_address.data(), host_address.length());
    headers.addReferenceKey(Envoy::Http::Headers::get().SetCookie,
                            factory_.makeSetCookie(encoded_address));
  }
}

CookieBasedSessionStateFactory::CookieBasedSessionStateFactory(
    const CookieBasedSessionStateProto& config)
    : name_(config.cookie().name()), ttl_(config.cookie().ttl().seconds()),
      path_(config.cookie().path()) {
  if (name_.empty()) {
    throw EnvoyException("Cookie key cannot be empty for cookie based stateful sessions");
  }
}

} // namespace Cookie
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
