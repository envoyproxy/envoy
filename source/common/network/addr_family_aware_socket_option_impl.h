#pragma once

#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>

#include "envoy/network/listen_socket.h"

#include "common/common/logger.h"
#include "common/network/socket_option_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Network {

class AddrFamilyAwareSocketOptionImpl : public Socket::Option,
                                        Logger::Loggable<Logger::Id::connection> {
public:
  AddrFamilyAwareSocketOptionImpl(Socket::SocketState in_state, SocketOptionName ipv4_optname,
                                  SocketOptionName ipv6_optname, int value)
      : ipv4_option_(absl::make_unique<SocketOptionImpl>(in_state, ipv4_optname, value)),
        ipv6_option_(absl::make_unique<SocketOptionImpl>(in_state, ipv6_optname, value)) {}

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
  static bool setIpSocketOption(Socket& socket, Socket::SocketState state,
                                const std::unique_ptr<SocketOptionImpl>& ipv4_option,
                                const std::unique_ptr<SocketOptionImpl>& ipv6_option);

private:
  const std::unique_ptr<SocketOptionImpl> ipv4_option_;
  const std::unique_ptr<SocketOptionImpl> ipv6_option_;
};

} // namespace Network
} // namespace Envoy
