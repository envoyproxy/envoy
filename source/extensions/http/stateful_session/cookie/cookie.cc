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

  // If no cookie path is specified or root cookie path is specified then this session state will
  // be enabled for any request.
  if (path_.empty() || path_ == "/") {
    path_matcher_ = [](absl::string_view) { return true; };
    return;
  }

  // If specified cookie path is ends with '/' then this session state will be enabled for the
  // requests with a path that starts with the cookie path.
  // For example the cookie path '/foo/' will matches request paths '/foo/bar' or '/foo/dir', but
  // will not match request path '/foo'.
  if (absl::EndsWith(path_, "/")) {
    path_matcher_ = [path = path_](absl::string_view request_path) {
      return absl::StartsWith(request_path, path);
    };
    return;
  }

  path_matcher_ = [path = path_](absl::string_view request_path) {
    if (absl::StartsWith(request_path, path)) {
      // Request path is same with cookie path.
      if (request_path.size() == path.size()) {
        return true;
      }

      // The next character of the matching part should be the slash ('/'), question mark ('?') or
      // number sign ('#').
      ASSERT(request_path.size() > path.size());
      const char next_char = request_path[path.size()];
      if (next_char == '/' || next_char == '?' || next_char == '#') {
        return true;
      }
    }
    return false;
  };
}

} // namespace Cookie
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
