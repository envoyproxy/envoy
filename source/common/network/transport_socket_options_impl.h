#pragma once

#include "envoy/network/transport_socket.h"

namespace Envoy {
namespace Network {

class TransportSocketOptionsImpl : public TransportSocketOptions {
public:
  TransportSocketOptionsImpl(absl::string_view override_server_name = "")
      : override_server_name_(override_server_name.empty()
                                  ? absl::nullopt
                                  : absl::optional<std::string>(override_server_name)) {}

  // Network::TransportSocketOptions
  const absl::optional<std::string>& serverNameOverride() const override {
    return override_server_name_;
  }
  void hashKey(std::vector<uint8_t>& key) const override;

private:
  const absl::optional<std::string> override_server_name_;
};

} // namespace Network
} // namespace Envoy
