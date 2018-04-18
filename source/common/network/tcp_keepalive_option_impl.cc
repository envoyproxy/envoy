#include "common/network/tcp_keepalive_option_impl.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/network/address_impl.h"

namespace Envoy {
namespace Network {
bool TcpKeepaliveOptionImpl::setOption(Network::Socket& socket,
                                       Network::Socket::SocketState state) const {
  if (state == Socket::SocketState::PreBind) {
    return setTcpKeepalive(socket, keepalive_config_.keepalive_probes_,
                           keepalive_config_.keepalive_time_,
                           keepalive_config_.keepalive_interval_);
  }
  return true;
}

bool TcpKeepaliveOptionImpl::setTcpKeepalive(Socket& socket, absl::optional<int> keepalive_probes,
                                             absl::optional<int> keepalive_time,
                                             absl::optional<int> keepalive_interval) {
  int error;
  error = setSocketOption(socket, SOL_SOCKET, ENVOY_SOCKET_SO_KEEPALIVE, 1);
  if (error != 0) {
    ENVOY_LOG(warn, "Setting SO_KEEPALIVE on socket failed: {}", strerror(errno));
    return false;
  }
  error = setSocketOption(socket, IPPROTO_TCP, ENVOY_SOCKET_TCP_KEEPCNT, keepalive_probes);
  if (error != 0) {
    ENVOY_LOG(warn, "Setting keepalive_probes failed: {}", strerror(errno));
    return false;
  }
  error = setSocketOption(socket, IPPROTO_TCP, ENVOY_SOCKET_TCP_KEEPIDLE, keepalive_time);
  if (error != 0) {
    ENVOY_LOG(warn, "Setting keepalive_time failed: {}", strerror(errno));
    return false;
  }
  error = setSocketOption(socket, IPPROTO_TCP, ENVOY_SOCKET_TCP_KEEPINTVL, keepalive_interval);
  if (error != 0) {
    ENVOY_LOG(warn, "Setting keepalive_interval failed: {}", strerror(errno));
    return false;
  }
  return true;
}

int TcpKeepaliveOptionImpl::setSocketOption(Socket& socket, int level,
                                            Network::SocketOptionName optname,
                                            absl::optional<int> optional_value) {
  if (optional_value.has_value()) {
    return setSocketOption(socket, level, optname, optional_value.value());
  } else {
    return 0;
  }
}

int TcpKeepaliveOptionImpl::setSocketOption(Socket& socket, int level,
                                            Network::SocketOptionName optname, int value) {
  if (!optname.has_value()) {
    errno = ENOTSUP;
    return -1;
  }
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  return os_syscalls.setsockopt(socket.fd(), level, optname.value(), &value, sizeof(value));
}

} // namespace Network
} // namespace Envoy
