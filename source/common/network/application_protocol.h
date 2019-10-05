#pragma once

#include "envoy/http/protocol.h"
#include "envoy/stream_info/filter_state.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Network {

using ApplicationProtocolOverride = absl::flat_hash_map<Http::Protocol, std::vector<std::string>>;

/**
 * ALPN to set in the upstream connection. Filters can use this one to override the ALPN in TLS
 * context.
 */
class ApplicationProtocols : public StreamInfo::FilterState::Object {
public:
  ApplicationProtocols(const ApplicationProtocolOverride& http_alpn_override,
                       const std::vector<std::string>& tcp_alpn_override)
      : http_alpn_override_(http_alpn_override), tcp_alpn_override_(tcp_alpn_override) {}

  bool hasProtocol(const Http::Protocol& protocol) const {
    return http_alpn_override_.count(protocol) > 0;
  }

  const std::vector<std::string>& value(const Http::Protocol& protocol) const {
    return http_alpn_override_.at(protocol);
  }

  const std::vector<std::string>& value() const { return tcp_alpn_override_; }

  static const std::string& key();

private:
  const ApplicationProtocolOverride http_alpn_override_;
  const std::vector<std::string> tcp_alpn_override_;
};

} // namespace Network
} // namespace Envoy
