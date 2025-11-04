#pragma once

#include "envoy/stream_info/filter_state.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Network {

/**
 * Network namespace filepath from the listener address. This filter state is automatically
 * populated when a connection is accepted if the listener's local address has a network
 * namespace configured. It provides read-only access to the network namespace, which is
 * particularly useful for logging, routing decisions, or other filter logic in multi-tenant
 * or containerized environments.
 */
class DownstreamNetworkNamespace : public StreamInfo::FilterState::Object {
public:
  DownstreamNetworkNamespace(absl::string_view network_namespace_filepath)
      : network_namespace_filepath_(network_namespace_filepath) {}
  const std::string& value() const { return network_namespace_filepath_; }
  absl::optional<std::string> serializeAsString() const override {
    return network_namespace_filepath_;
  }
  static const std::string& key();

private:
  const std::string network_namespace_filepath_;
};

} // namespace Network
} // namespace Envoy
