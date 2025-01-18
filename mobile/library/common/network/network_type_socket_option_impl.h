#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/socket.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Network {

/**
 * This is a "dummy" socket option implementation, which does not actually
 * set any socket option, but rather applies a network type tag to the socket so that requests from
 * different network types gets hashed to different connections.
 */
class NetworkTypeSocketOptionImpl : public Network::Socket::Option {
public:
  NetworkTypeSocketOptionImpl(int network_type);

  // Socket::Option
  bool setOption(Network::Socket& socket,
                 envoy::config::core::v3::SocketOption::SocketState state) const override;
  void hashKey(std::vector<uint8_t>& hash_key) const override;
  absl::optional<Details>
  getOptionDetails(const Network::Socket& socket,
                   envoy::config::core::v3::SocketOption::SocketState state) const override;
  bool isSupported() const override;

private:
  const Network::SocketOptionName optname_;

  int network_type_;
};

} // namespace Network
} // namespace Envoy
