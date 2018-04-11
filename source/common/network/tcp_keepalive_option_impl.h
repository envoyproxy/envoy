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
  TcpKeepaliveOptionImpl(absl::optional<uint32_t> keepalive_probes, absl::optional<uint32_t> keepalive_time,
                         absl::optional<uint32_t> keepalive_interval)
      : keepalive_probes_(keepalive_probes), keepalive_time_(keepalive_time),
        keepalive_interval_(keepalive_interval) {}

  // Socket::Option
  bool setOption(Socket& socket, Socket::SocketState state) const override;

  // The tcp keepalive options don't require a hash key.
  void hashKey(std::vector<uint8_t>&) const override {}

  static bool setTcpKeepalive(Socket& socket, absl::optional<uint32_t> keepalive_probes,
                              absl::optional<uint32_t> keepalive_time,
                              absl::optional<uint32_t> keepalive_interval);

private:
  absl::optional<uint32_t> keepalive_probes_;
  absl::optional<uint32_t> keepalive_time_;
  absl::optional<uint32_t> keepalive_interval_;
};
} // namespace Network
} // namespace Envoy
