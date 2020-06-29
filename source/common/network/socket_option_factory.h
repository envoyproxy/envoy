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
  static Socket::OptionsPtr buildTcpKeepaliveOptions(Network::TcpKeepaliveConfig keepalive_config);
  static Socket::OptionsPtr buildIpFreebindOptions();
  static Socket::OptionsPtr buildIpTransparentOptions();
  static Socket::OptionsPtr buildSocketMarkOptions(uint32_t mark);
  static Socket::OptionsPtr buildTcpFastOpenOptions(uint32_t queue_length);
  static Socket::OptionsPtr buildLiteralOptions(
      const Protobuf::RepeatedPtrField<envoy::config::core::v3::SocketOption>& socket_options);
  static Socket::OptionsPtr buildIpPacketInfoOptions();
  static Socket::OptionsPtr buildRxQueueOverFlowOptions();
  static Socket::OptionsPtr buildReusePortOptions();
};
} // namespace Network
} // namespace Envoy
