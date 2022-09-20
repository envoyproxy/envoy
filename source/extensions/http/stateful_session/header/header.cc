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
    headers.addCopy(Envoy::Http::LowerCaseString(factory_.getHeaderName()), encoded_address);
  }
}

HeaderBasedSessionStateFactory::HeaderBasedSessionStateFactory(
    const HeaderBasedSessionStateProto& config)
    : name_(config.name()), path_(config.path()) {
  if (config.name().empty()) {
    throw EnvoyException("Header name cannot be empty for header based stateful sessions");
  }

  // If no request path is specified or root path is specified then this session state will
  // be enabled for any request
  if (path_.empty() || path_ == "/") {
    path_matcher_ = [](absl::string_view) { return true; };
    return;
  }

  if (absl::EndsWith(path_, "/")) {
    path_matcher_ = [path = path_](absl::string_view request_path) {
      return absl::StartsWith(request_path, path);
    };
    return;
  }

  path_matcher_ = [path = path_](absl::string_view request_path) {
    if (absl::StartsWith(request_path, path)) {
      // Request path matches exactly
      if (request_path.size() == path.size()) {
        return true;
      }

      ASSERT(request_path.size() > path.size());
      const char next_char = request_path[path.size()];
      if (next_char == '/' || next_char == '?' || next_char == '#') {
        return true;
      }
    }
    return false;
  };
}

} // namespace Header
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
