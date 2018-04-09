#include "common/network/tcp_keepalive_option_impl.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/network/address_impl.h"

namespace Envoy {
namespace Network {
bool TcpKeepaliveOptionImpl::setOption(Network::Socket& socket,
                                       Network::Socket::SocketState state) const {
  if (state == Socket::SocketState::PreBind) {
    return setTcpKeepalive(socket, keepalive_probes_, keepalive_time_, keepalive_interval_);
  }
  return true;
}

bool TcpKeepaliveOptionImpl::setTcpKeepalive(Socket& socket, absl::optional<int> keepalive_probes,
                                             absl::optional<int> keepalive_time,
                                             absl::optional<int> keepalive_interval) {
  if (!ENVOY_SOCKET_SO_KEEPALIVE.has_value()) {
    ENVOY_LOG(warn, "TCP keepalive not supported on this platform");
    return false;
  }
  auto& os_syscalls = Api::OsSysCallsSingleton::get();

  const int optOne = 1;
  const socklen_t optOneLen = sizeof(optOne);
  int error = os_syscalls.setsockopt(socket.fd(), SOL_SOCKET, ENVOY_SOCKET_SO_KEEPALIVE.value(),
                                     &optOne, optOneLen);
  if (error != 0) {
    ENVOY_LOG(warn, "Setting SO_KEEPALIVE on socket failed: {}", strerror(error));
    return false;
  }
  if (keepalive_probes.has_value()) {
    if (!ENVOY_SOCKET_TCP_KEEPCNT.has_value()) {
      ENVOY_LOG(warn, "TCP keepalive_probes not supported on this platform");
      return false;
    }
    error = os_syscalls.setsockopt(socket.fd(), IPPROTO_TCP, ENVOY_SOCKET_TCP_KEEPCNT.value(),
                                   &keepalive_probes.value(), sizeof(keepalive_probes.value()));
    if (error != 0) {
      ENVOY_LOG(warn, "Setting TCP_KEEPCNT on socket failed: {}", strerror(error));
      return false;
    }
  }
  if (keepalive_time.has_value()) {
    if (!ENVOY_SOCKET_TCP_KEEPIDLE.has_value()) {
      ENVOY_LOG(warn, "TCP keepalive_time not supported on this platform");
      return false;
    }
    error = os_syscalls.setsockopt(socket.fd(), IPPROTO_TCP, ENVOY_SOCKET_TCP_KEEPIDLE.value(),
                                   &keepalive_time.value(), sizeof(keepalive_time.value()));
    if (error != 0) {
      ENVOY_LOG(warn, "Setting TCP_KEEPIDLE on socket failed: {}", strerror(error));
      return false;
    }
  }
  if (keepalive_interval.has_value()) {
    if (!ENVOY_SOCKET_TCP_KEEPINTVL.has_value()) {
      ENVOY_LOG(warn, "TCP keepalive_interval not supported on this platform");
      return false;
    }
    error = os_syscalls.setsockopt(socket.fd(), IPPROTO_TCP, ENVOY_SOCKET_TCP_KEEPINTVL.value(),
                                   &keepalive_interval.value(), sizeof(keepalive_interval.value()));
    if (error != 0) {
      ENVOY_LOG(warn, "Setting TCP_KEEPINTVL on socket failed: {}", strerror(error));
      return false;
    }
  }
  return true;
}

} // namespace Network
} // namespace Envoy