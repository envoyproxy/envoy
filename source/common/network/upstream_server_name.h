#pragma once

#include "envoy/stream_info/filter_state.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Network {

/**
 * Server name to set in the upstream connection. The filters like tcp_proxy should use this
 * value to override the server name specified in the upstream cluster, for example to override
 * the SNI value in the upstream TLS context.
 */
class UpstreamServerName : public StreamInfo::FilterState::Object {
public:
  UpstreamServerName(absl::string_view server_name) : server_name_(server_name) {}
  const std::string& value() const { return server_name_; }
  static const std::string& key();

private:
  const std::string server_name_;
};

} // namespace Network
} // namespace Envoy
