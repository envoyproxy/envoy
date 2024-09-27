#pragma once

#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/socket.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"

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
  static std::unique_ptr<Socket::Options>
  buildWFPRedirectRecordsOptions(const Win32RedirectRecords& redirect_records);
  static std::unique_ptr<Socket::Options> buildSocketMarkOptions(uint32_t mark);
  static std::unique_ptr<Socket::Options> buildSocketNoSigpipeOptions();
  static std::unique_ptr<Socket::Options> buildTcpFastOpenOptions(uint32_t queue_length);
  static std::unique_ptr<Socket::Options> buildLiteralOptions(
      const Protobuf::RepeatedPtrField<envoy::config::core::v3::SocketOption>& socket_options);
  static std::unique_ptr<Socket::Options> buildIpPacketInfoOptions();
  static std::unique_ptr<Socket::Options> buildRxQueueOverFlowOptions();
  static std::unique_ptr<Socket::Options> buildReusePortOptions();
  static std::unique_ptr<Socket::Options> buildUdpGroOptions();
  static std::unique_ptr<Socket::Options> buildZeroSoLingerOptions();
  static std::unique_ptr<Socket::Options> buildIpRecvTosOptions();
  /**
   * @param supports_v4_mapped_v6_addresses true if this option is to be applied to a v6 socket with
   * v4-mapped v6 address(i.e. ::ffff:172.21.0.6) support.
   */
  static std::unique_ptr<Socket::Options>
  buildDoNotFragmentOptions(bool supports_v4_mapped_v6_addresses);
};
} // namespace Network
} // namespace Envoy
