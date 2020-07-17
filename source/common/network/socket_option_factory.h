#pragma once

#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/socket.h"

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
  static std::unique_ptr<Socket::Options> buildSocketMarkOptions(uint32_t mark);
  static std::unique_ptr<Socket::Options> buildSocketNoSigpipeOptions();
  static std::unique_ptr<Socket::Options> buildTcpFastOpenOptions(uint32_t queue_length);
  static std::unique_ptr<Socket::Options> buildLiteralOptions(
      const Protobuf::RepeatedPtrField<envoy::config::core::v3::SocketOption>& socket_options);
  static std::unique_ptr<Socket::Options> buildIpPacketInfoOptions();
  static std::unique_ptr<Socket::Options> buildRxQueueOverFlowOptions();
  static std::unique_ptr<Socket::Options> buildReusePortOptions();
  static std::unique_ptr<Socket::Options> buildUdpGroOptions();
};
} // namespace Network
} // namespace Envoy
