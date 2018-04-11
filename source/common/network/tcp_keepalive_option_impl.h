#pragma once

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "envoy/network/listen_socket.h"

#include "common/common/logger.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Network {

// Optional variant of setsockopt(2) optname. The idea here is that if the option is not supported
// on a platform, we can make this the empty value. This allows us to avoid proliferation of #ifdef.
typedef absl::optional<int> SocketOptionName;

#ifdef SO_KEEPALIVE
#define ENVOY_SOCKET_SO_KEEPALIVE Network::SocketOptionName(SO_KEEPALIVE)
#else
#define ENVOY_SOCKET_SO_KEEPALIVE Network::SocketOptionName()
#endif

#ifdef TCP_KEEPCNT
#define ENVOY_SOCKET_TCP_KEEPCNT Network::SocketOptionName(TCP_KEEPCNT)
#else
#define ENVOY_SOCKET_TCP_KEEPCNT Network::SocketOptionName()
#endif

#ifdef TCP_KEEPIDLE
#define ENVOY_SOCKET_TCP_KEEPIDLE Network::SocketOptionName(TCP_KEEPIDLE)
#elif TCP_KEEPALIVE // OS X I think
#define ENVOY_SOCKET_TCP_KEEPIDLE Network::SocketOptionName(TCP_KEEPALIVE)
#else
#define ENVOY_SOCKET_TCP_KEEPIDLE Network::SocketOptionName()
#endif

#ifdef TCP_KEEPINTVL
#define ENVOY_SOCKET_TCP_KEEPINTVL Network::SocketOptionName(TCP_KEEPINTVL)
#else
#define ENVOY_SOCKET_TCP_KEEPINTVL Network::SocketOptionName()
#endif

class TcpKeepaliveOptionImpl : public Socket::Option, Logger::Loggable<Logger::Id::connection> {
public:
  TcpKeepaliveOptionImpl(Network::TcpKeepaliveConfig keepalive_config)
      : keepalive_config_(keepalive_config) {}

  // Socket::Option
  bool setOption(Socket& socket, Socket::SocketState state) const override;

  // The tcp keepalive options don't require a hash key.
  void hashKey(std::vector<uint8_t>&) const override {}

  static bool setTcpKeepalive(Socket& socket, const absl::optional<uint32_t>& keepalive_probes,
                              const absl::optional<uint32_t>& keepalive_time,
                              const absl::optional<uint32_t>& keepalive_interval);

private:
  Network::TcpKeepaliveConfig keepalive_config_;
};
} // namespace Network
} // namespace Envoy
