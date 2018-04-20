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

class SocketOptionFactory : Logger::Loggable<Logger::Id::connection> {
public:
  static std::unique_ptr<Socket::Option>
  buildTcpKeepaliveOptions(Network::TcpKeepaliveConfig keepalive_config);
  static std::unique_ptr<Socket::Option> buildIpFreebindOptions();
  static std::unique_ptr<Socket::Option> buildIpTransparentOptions();
  static std::unique_ptr<Socket::Option> buildTcpFastOpenOptions(uint32_t queue_length);
};
} // namespace Network
} // namespace Envoy
