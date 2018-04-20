#pragma once

#include <netinet/tcp.h>

#include "envoy/api/v2/listener/listener.pb.h"

#include "common/network/listen_socket_impl.h"
#include "common/network/socket_option_impl.h"

#include "server/configuration_impl.h"

namespace Envoy {
namespace Server {

// Socket::Option implementation for API-defined listener socket options.
// This same object can be extended to handle additional listener socket options.
class ListenerSocketOptionImpl : public Network::SocketOptionImpl {
public:
  ListenerSocketOptionImpl(const envoy::api::v2::Listener& config);

  ListenerSocketOptionImpl(absl::optional<bool> transparent, absl::optional<bool> freebind,
                           absl::optional<uint32_t> tcp_fast_open_queue_length);

  // Socket::Option
  bool setOption(Network::Socket& socket, Network::Socket::SocketState state) const override;

private:
  const absl::optional<uint32_t> tcp_fast_open_queue_length_;
};

} // namespace Server
} // namespace Envoy
