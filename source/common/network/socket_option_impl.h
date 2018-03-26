#pragma once

#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>

#include "envoy/network/listen_socket.h"

#include "common/common/logger.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Network {

// Optional variant of setsockopt(2) optname. The idea here is that if the option is not supported
// on a platform, we can make this the empty value. This allows us to avoid proliferation of #ifdef.
typedef absl::optional<int> SocketOptionName;

#ifdef IP_FREEBIND
#define ENVOY_SOCKET_IP_FREEBIND Network::SocketOptionName(IP_FREEBIND)
#else
#define ENVOY_SOCKET_IP_FREEBIND Network::SocketOptionName()
#endif

#ifdef IPV6_FREEBIND
#define ENVOY_SOCKET_IPV6_FREEBIND Network::SocketOptionName(IPV6_FREEBIND)
#else
#define ENVOY_SOCKET_IPV6_FREEBIND Network::SocketOptionName()
#endif

class SocketOptionImpl : public Socket::Option, Logger::Loggable<Logger::Id::connection> {
public:
  SocketOptionImpl(absl::optional<bool> freebind) : freebind_(freebind) {}

  // Socket::Option
  bool setOption(Socket& socket, Socket::SocketState state) const override;
  // The common socket options don't require a hash key.
  void hashKey(std::vector<uint8_t>&) const override {}

  /**
   * Set a socket option that applies at both IPv4 and IPv6 socket levels. When the underlying FD
   * is IPv6, this function will attempt to set at IPv6 unless the platform only supports the
   * option at the IPv4 level.
   * @param socket.
   * @param ipv4_optname SocketOptionName for IPv4 level. Set to empty if not supported on
   * platform.
   * @param ipv6_optname SocketOptionName for IPv6 level. Set to empty if not supported on
   * platform.
   * @param optval as per setsockopt(2).
   * @param optlen as per setsockopt(2).
   * @return int as per setsockopt(2). ENOTSUP is returned if the option is not supported on the
   * platform for fd after the above option level fallback semantics are taken into account or the
   *         socket is non-IP.
   */
  static int setIpSocketOption(Socket& socket, SocketOptionName ipv4_optname,
                               SocketOptionName ipv6_optname, const void* optval, socklen_t optlen);

private:
  const absl::optional<bool> freebind_;
};

} // namespace Network
} // namespace Envoy
