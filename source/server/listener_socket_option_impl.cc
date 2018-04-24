#include "server/listener_socket_option_impl.h"

#include "common/api/os_sys_calls_impl.h"

namespace Envoy {
namespace Server {

ListenerSocketOptionImpl::ListenerSocketOptionImpl(const envoy::api::v2::Listener& config)
    : Network::SocketOptionImpl(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, transparent, absl::optional<bool>{}),
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, freebind, absl::optional<bool>{})),
      tcp_fast_open_queue_length_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          config, tcp_fast_open_queue_length, absl::optional<uint32_t>{})) {}

ListenerSocketOptionImpl::ListenerSocketOptionImpl(
    absl::optional<bool> transparent, absl::optional<bool> freebind,
    absl::optional<uint32_t> tcp_fast_open_queue_length)
    : Network::SocketOptionImpl(transparent, freebind),
      tcp_fast_open_queue_length_(tcp_fast_open_queue_length) {}

bool ListenerSocketOptionImpl::setOption(Network::Socket& socket,
                                         Network::Socket::SocketState state) const {
  bool result = Network::SocketOptionImpl::setOption(socket, state);
  if (!result) {
    return result;
  }

  if (state == Network::Socket::SocketState::Listening) {
    if (tcp_fast_open_queue_length_.has_value()) {
      const int tfo_value = tcp_fast_open_queue_length_.value();
      const Network::SocketOptionName option_name = ENVOY_SOCKET_TCP_FASTOPEN;
      if (option_name) {
        const int error = Api::OsSysCallsSingleton::get().setsockopt(
            socket.fd(), IPPROTO_TCP, option_name.value(),
            reinterpret_cast<const void*>(&tfo_value), sizeof(tfo_value));
        if (error != 0) {
          ENVOY_LOG(warn, "Setting TCP_FASTOPEN on listener socket failed: {}", strerror(errno));
          return false;
        } else {
          ENVOY_LOG(debug, "Successfully set socket option TCP_FASTOPEN to {}", tfo_value);
        }
      } else {
        ENVOY_LOG(warn, "Unsupported socket option TCP_FASTOPEN");
        return false;
      }
    }
  }

  return true;
}

} // namespace Server
} // namespace Envoy
