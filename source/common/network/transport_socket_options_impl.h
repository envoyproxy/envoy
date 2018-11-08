#pragma once

#include "envoy/network/transport_socket.h"

namespace Envoy {
namespace Network {

class TransportSocketOptionsImpl : public TransportSocketOptions {
public:
  TransportSocketOptionsImpl(std::string override_server_name = "");
  const absl::optional<std::string>& overrideServerName() const override {
    return override_server_name_;
  }
  void hashKey(std::vector<uint8_t>& key) const override;

private:
  absl::optional<std::string> override_server_name_;
};

} // namespace Network
} // namespace Envoy
