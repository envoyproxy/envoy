#include "common/network/socket_option_impl.h"

#include "envoy/common/exception.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/network/address_impl.h"

namespace Envoy {
namespace Network {

bool SocketOptionImpl::setOption(Socket& socket, Socket::SocketState state) const {
  if (state == Socket::SocketState::PreBind || state == Socket::SocketState::PostBind) {
    if (transparent_.has_value()) {
      const int should_transparent = transparent_.value() ? 1 : 0;
      const int error =
          setIpSocketOption(socket, ENVOY_SOCKET_IP_TRANSPARENT, ENVOY_SOCKET_IPV6_TRANSPARENT,
                            &should_transparent, sizeof(should_transparent));
      if (error != 0) {
        ENVOY_LOG(warn, "Setting IP_TRANSPARENT on listener socket failed: {}", strerror(error));
        return false;
      }
    }
  }

  if (state == Socket::SocketState::PreBind) {
    if (freebind_.has_value()) {
      const int should_freebind = freebind_.value() ? 1 : 0;
      const int error =
          setIpSocketOption(socket, ENVOY_SOCKET_IP_FREEBIND, ENVOY_SOCKET_IPV6_FREEBIND,
                            &should_freebind, sizeof(should_freebind));
      if (error != 0) {
        ENVOY_LOG(warn, "Setting IP_FREEBIND on listener socket failed: {}", strerror(errno));
        return false;
      }
    }
  }

  return true;
}

int SocketOptionImpl::setIpSocketOption(Socket& socket, SocketOptionName ipv4_optname,
                                        SocketOptionName ipv6_optname, const void* optval,
                                        socklen_t optlen) {
  auto& os_syscalls = Api::OsSysCallsSingleton::get();

  // If this isn't IP, we're out of luck.
  Address::InstanceConstSharedPtr address;
  const Address::Ip* ip = nullptr;
  try {
    // We have local address when the socket is used in a listener but have to
    // infer the IP from the socket FD when initiating connections.
    // TODO(htuch): Figure out a way to obtain a consistent interface for IP
    // version from socket.
    if (socket.localAddress()) {
      ip = socket.localAddress()->ip();
    } else {
      address = Address::addressFromFd(socket.fd());
      ip = address->ip();
    }
  } catch (const EnvoyException&) {
    // Ignore, we get here because we failed in getsockname().
    // TODO(htuch): We should probably clean up this logic to avoid relying on exceptions.
  }
  if (ip == nullptr) {
    ENVOY_LOG(warn, "Failed to set IP socket option on non-IP socket");
    return ENOTSUP;
  }

  // If the FD is v4, we can only try the IPv4 variant.
  if (ip->version() == Network::Address::IpVersion::v4) {
    if (!ipv4_optname) {
      ENVOY_LOG(warn, "Unsupported IPv4 socket option");
      errno = ENOTSUP;
      return -1;
    }
    return os_syscalls.setsockopt(socket.fd(), IPPROTO_IP, ipv4_optname.value(), optval, optlen);
  }

  // If the FD is v6, we first try the IPv6 variant if the platfrom supports it and fallback to the
  // IPv4 variant otherwise.
  ASSERT(ip->version() == Network::Address::IpVersion::v6);
  if (ipv6_optname) {
    return os_syscalls.setsockopt(socket.fd(), IPPROTO_IPV6, ipv6_optname.value(), optval, optlen);
  }
  if (ipv4_optname) {
    return os_syscalls.setsockopt(socket.fd(), IPPROTO_IP, ipv4_optname.value(), optval, optlen);
  }
  ENVOY_LOG(warn, "Unsupported IPv6 socket option");
  errno = ENOTSUP;
  return -1;
}

} // namespace Network
} // namespace Envoy
