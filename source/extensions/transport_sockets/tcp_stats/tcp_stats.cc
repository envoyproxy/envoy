#if defined(__linux__)

// `struct tcp_info` is defined in two places: /usr/include/netinet/tcp.h (included from
// envoy/common/platform.h) and /usr/include/linux/tcp.h. The former version is older and doesn't
// contain all the fields needed.  Including both headers results in a compilation error due to the
// duplicate (and different) definitions of `struct tcp_info`. To work around this, define
// `DO_NOT_INCLUDE_NETINET_TCP_H` to prevent inclusion of the wrong version.
#include </usr/include/linux/tcp.h>
#define DO_NOT_INCLUDE_NETINET_TCP_H 1

#include "source/extensions/transport_sockets/tcp_stats/tcp_stats.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace TcpStats {

Config::Config(const envoy::extensions::transport_sockets::tcp_stats::v3::Config& config_proto,
               Stats::Scope& scope)
    : stats_(generateStats(scope)),
      update_period_(PROTOBUF_GET_OPTIONAL_MS(config_proto, update_period)) {}

LinuxNetworkStats Config::generateStats(Stats::Scope& scope) {
  const std::string prefix("tcp_stats");
  return LinuxNetworkStats{ALL_LINUX_NETWORK_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                                   POOL_GAUGE_PREFIX(scope, prefix),
                                                   POOL_HISTOGRAM_PREFIX(scope, prefix))};
}

TcpStatsSocket::TcpStatsSocket(ConfigConstSharedPtr config,
                               Network::TransportSocketPtr inner_socket)
    : PassthroughSocket(std::move(inner_socket)), config_(std::move(config)) {}

void TcpStatsSocket::setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) {
  callbacks_ = &callbacks;
  transport_socket_->setTransportSocketCallbacks(callbacks);
}

void TcpStatsSocket::onConnected() {
  if (config_->update_period_.has_value()) {
    timer_ = callbacks_->connection().dispatcher().createTimer([this]() {
      auto tcp_info = querySocketInfo();
      updateCountersAndGauges(tcp_info);
      timer_->enableTimer(config_->update_period_.value());
    });
    timer_->enableTimer(config_->update_period_.value());
  }

  transport_socket_->onConnected();
}

void TcpStatsSocket::closeSocket(Network::ConnectionEvent event) {
  // Record final values.
  auto info = querySocketInfo();
  updateCountersAndGauges(info);
  recordHistograms(info);

  transport_socket_->closeSocket(event);
}

struct tcp_info TcpStatsSocket::querySocketInfo() {
  struct tcp_info info;
  memset(&info, 0, sizeof(info));
  socklen_t optlen = sizeof(info);
  const auto result = callbacks_->ioHandle().getOption(IPPROTO_TCP, TCP_INFO, &info, &optlen);
  if (result.return_value_ == 0) {
    ASSERT(optlen == sizeof(info));
  } else {
    ENVOY_LOG(debug, "Failed TCP_INFO: {} {} {}", result.return_value_, result.errno_,
              strerror(result.errno_));
    // On error, ensure that all values are zero for predictable results.
    memset(&info, 0, sizeof(info));
  }
  return info;
}

void TcpStatsSocket::updateCountersAndGauges(struct tcp_info& tcp_info) {
  ENVOY_LOG(info, "updateCountersAndGauges");
  auto update_counter = [](Stats::Counter& counter, uint64_t& last_value, uint64_t current_value) {
    int64_t diff = static_cast<int64_t>(current_value) - static_cast<int64_t>(last_value);
    ASSERT(diff >= 0);
    if (diff > 0) {
      counter.add(diff);
    }
    last_value = current_value;
  };

  auto update_gauge = [](Stats::Gauge& gauge, uint64_t& last_value, uint64_t current_value) {
    int64_t diff = static_cast<int64_t>(current_value) - static_cast<int64_t>(last_value);
    gauge.add(diff);
    last_value = current_value;
  };

  update_counter(config_->stats_.cx_tx_segments_, last_cx_tx_segments_, tcp_info.tcpi_segs_out);
  update_counter(config_->stats_.cx_tx_retransmitted_segments_, last_cx_tx_retransmitted_segments_,
                 tcp_info.tcpi_total_retrans);

  update_gauge(config_->stats_.cx_tx_unsent_bytes_, last_cx_tx_unsent_bytes_,
               tcp_info.tcpi_notsent_bytes);
}

void TcpStatsSocket::recordHistograms(struct tcp_info& tcp_info) {
  if (tcp_info.tcpi_data_segs_out > 0) {
    // uint32 * uint32 cannot overflow a uint64, so this can safely be done as integer math instead
    // of floating point.
    static_assert((sizeof(tcp_info.tcpi_total_retrans) == sizeof(uint32_t)) &&
                  (Stats::Histogram::PercentScale < UINT32_MAX));
    const uint64_t percent =
        (static_cast<uint64_t>(tcp_info.tcpi_total_retrans) * Stats::Histogram::PercentScale) /
        static_cast<uint64_t>(tcp_info.tcpi_data_segs_out);
    ENVOY_CONN_LOG(trace, "Percent tcp retransmissions: {}", callbacks_->connection(),
                   static_cast<float>(percent) /
                       static_cast<float>(Stats::Histogram::PercentScale));
    config_->stats_.cx_tx_percent_retransmitted_segments_.recordValue(percent);
  }

  if (tcp_info.tcpi_min_rtt != 0) {
    config_->stats_.cx_min_rtt_us_.recordValue(tcp_info.tcpi_min_rtt);
  }

  if (tcp_info.tcpi_rtt != 0) {
    config_->stats_.cx_rtt_us_.recordValue(tcp_info.tcpi_rtt);
    config_->stats_.cx_rttvar_.recordValue(tcp_info.tcpi_rttvar);
  }
  ENVOY_CONN_LOG(trace, "tcpi_min_rtt {}, tcpi_rtt {}, tcpi_rttvar {}", callbacks_->connection(),
                 tcp_info.tcpi_min_rtt, tcp_info.tcpi_rtt, tcp_info.tcpi_rttvar);
}

} // namespace TcpStats
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
#endif // defined(__linux__)
