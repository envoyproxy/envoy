#pragma once

#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace Network {

/**
 * ALPN to set in the upstream connection. Filters can use this one to override the ALPN in TLS
 * context.
 */
class ApplicationProtocols : public StreamInfo::FilterState::Object {
public:
  explicit ApplicationProtocols(const std::vector<std::string>& application_protocols)
      : application_protocols_(application_protocols) {}
  const std::vector<std::string>& value() const { return application_protocols_; }
  static const std::string& key();

private:
  const std::vector<std::string> application_protocols_;
};

} // namespace Network
} // namespace Envoy
