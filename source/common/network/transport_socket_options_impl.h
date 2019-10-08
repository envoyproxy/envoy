#pragma once

#include "envoy/network/transport_socket.h"

namespace Envoy {
namespace Network {

class TransportSocketOptionsImpl : public TransportSocketOptions {
public:
  TransportSocketOptionsImpl(absl::string_view override_server_name = "",
                             std::vector<std::string>&& override_verify_san_list = {},
                             std::vector<std::string>&& override_alpn = {})
      : override_server_name_(override_server_name.empty()
                                  ? absl::nullopt
                                  : absl::optional<std::string>(override_server_name)),
        override_verify_san_list_{std::move(override_verify_san_list)},
        override_alpn_list_{std::move(override_alpn)} {}

  // Network::TransportSocketOptions
  const absl::optional<std::string>& serverNameOverride() const override {
    return override_server_name_;
  }
  const std::vector<std::string>& verifySubjectAltNameListOverride() const override {
    return override_verify_san_list_;
  }
  const std::vector<std::string>& applicationProtocolListOverride() const override {
    return override_alpn_list_;
  }
  void hashKey(std::vector<uint8_t>& key) const override;

private:
  const absl::optional<std::string> override_server_name_;
  const std::vector<std::string> override_verify_san_list_;
  const std::vector<std::string> override_alpn_list_;
};

} // namespace Network
} // namespace Envoy
