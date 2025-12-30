#pragma once

#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/socket.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Network {

struct TcpKeepaliveConfig {
  absl::optional<uint32_t>
      keepalive_probes_; // Number of unanswered probes before the connection is dropped
  absl::optional<uint32_t> keepalive_time_; // Connection idle time before probing will start, in ms
  absl::optional<uint32_t> keepalive_interval_; // Interval between probes, in ms
};

static inline Network::TcpKeepaliveConfig
parseTcpKeepaliveConfig(const envoy::config::core::v3::TcpKeepalive& options) {
  return Network::TcpKeepaliveConfig{PROTOBUF_GET_OPTIONAL_WRAPPED(options, keepalive_probes),
                                     PROTOBUF_GET_OPTIONAL_WRAPPED(options, keepalive_time),
                                     PROTOBUF_GET_OPTIONAL_WRAPPED(options, keepalive_interval)};
}

static inline bool isTcpKeepaliveConfigDisabled(const Network::TcpKeepaliveConfig& config) {
  return (config.keepalive_probes_.has_value() && config.keepalive_probes_.value() == 0) ||
         (config.keepalive_time_.has_value() && config.keepalive_time_.value() == 0) ||
         (config.keepalive_interval_.has_value() && config.keepalive_interval_.value() == 0);
}

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
  static std::unique_ptr<Socket::Options> buildBindAddressNoPort();
  /**
   * @param supports_v4_mapped_v6_addresses true if this option is to be applied to a v6 socket with
   * v4-mapped v6 address(i.e. ::ffff:172.21.0.6) support.
   */
  static std::unique_ptr<Socket::Options>
  buildDoNotFragmentOptions(bool supports_v4_mapped_v6_addresses);
};
} // namespace Network
} // namespace Envoy
