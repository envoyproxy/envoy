#pragma once

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "envoy/api/v2/core/address.pb.h"
#include "envoy/network/listen_socket.h"

#include "common/common/logger.h"
#include "common/protobuf/protobuf.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Network {

struct TcpKeepaliveConfig {
  absl::optional<uint32_t>
      keepalive_probes_; // Number of unanswered probes before the connection is dropped
  absl::optional<uint32_t> keepalive_time_; // Connection idle time before probing will start, in ms
  absl::optional<uint32_t> keepalive_interval_; // Interval between probes, in ms
};

class SocketOptionFactory : Logger::Loggable<Logger::Id::connection> {
public:
  static std::unique_ptr<Socket::Options>
  buildTcpKeepaliveOptions(Network::TcpKeepaliveConfig keepalive_config);
  static std::unique_ptr<Socket::Options> buildIpFreebindOptions();
  static std::unique_ptr<Socket::Options> buildIpTransparentOptions();
  static std::unique_ptr<Socket::Options> buildTcpFastOpenOptions(uint32_t queue_length);
  static std::unique_ptr<Socket::Options> buildLiteralOptions(
      const Protobuf::RepeatedPtrField<envoy::api::v2::core::SocketOption>& socket_options);
};
} // namespace Network
} // namespace Envoy
