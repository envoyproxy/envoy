#include "common/api/os_sys_calls_impl.h"

#include "server/listener_socket_option_impl.h"

namespace Envoy {
namespace Server {

bool ListenerSocketOptionImpl::setOption(Network::Socket& socket, Network::Socket::SocketState state) const {
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
      }
    }
  }

  return true;
}

} // namespace Server
} // namespace Envoy
