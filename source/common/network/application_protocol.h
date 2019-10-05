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
  explicit ApplicationProtocols(const ApplicationProtocolOverride& application_protocols_override)
      : application_protocols_override_(application_protocols_override) {}
  const ApplicationProtocolOverride& value() const { return application_protocols_override_; }
  static const std::string& key();

private:
  const ApplicationProtocolOverride application_protocols_override_;
};

} // namespace Network
} // namespace Envoy
