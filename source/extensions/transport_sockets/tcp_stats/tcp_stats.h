#pragma once

#if defined(__linux__)

#include "envoy/extensions/transport_sockets/tcp_stats/v3/tcp_stats.pb.h"

#include "envoy/event/timer.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/transport_sockets/common/passthrough.h"

// Defined in /usr/include/linux/tcp.h.
struct tcp_info;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace TcpStats {

#define ALL_LINUX_NETWORK_STATS(COUNTER, GAUGE, HISTOGRAM)                                         \
  COUNTER(cx_tx_segments)                                                                          \
  COUNTER(cx_tx_data_segments)                                                                     \
  COUNTER(cx_tx_retransmitted_segments)                                                            \
  GAUGE(cx_tx_unsent_bytes, Accumulate)                                                            \
  GAUGE(cx_tx_unacked_segments, Accumulate)                                                        \
  HISTOGRAM(cx_tx_percent_total_retransmitted_segments, Percent)                                   \
  HISTOGRAM(cx_rtt_us, Microseconds)                                                               \
  HISTOGRAM(cx_rttvar_us, Microseconds)                                                            \
  HISTOGRAM(cx_min_rtt_us, Microseconds)

struct LinuxNetworkStats {
  ALL_LINUX_NETWORK_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

class Config {
public:
  Config(const envoy::extensions::transport_sockets::tcp_stats::v3::Config& config_proto,
         Stats::Scope& scope);

  LinuxNetworkStats stats_;
  const absl::optional<std::chrono::milliseconds> update_period_;

private:
  LinuxNetworkStats generateStats(Stats::Scope& scope);
};

using ConfigConstSharedPtr = std::shared_ptr<const Config>;

class TcpStatsSocket : public TransportSockets::PassthroughSocket,
                       Logger::Loggable<Logger::Id::connection> {
public:
  TcpStatsSocket(ConfigConstSharedPtr config, Network::TransportSocketPtr inner_socket);

  // Network::TransportSocket
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  void onConnected() override;
  void closeSocket(Network::ConnectionEvent event) override;

private:
  struct tcp_info querySocketInfo();
  void recordPeriodicStats(struct tcp_info& tcp_info);
  void recordConnectionCloseStats(struct tcp_info& tcp_info);

  const ConfigConstSharedPtr config_;
  Network::TransportSocketCallbacks* callbacks_{};
  Event::TimerPtr timer_;

  uint32_t last_cx_tx_segments_{};
  uint32_t last_cx_tx_data_segments_{};
  uint32_t last_cx_tx_retransmitted_segments_{};
  uint32_t last_cx_tx_unsent_bytes_{};
  uint32_t last_cx_tx_unacked_segments_{};
};

} // namespace TcpStats
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

#endif // defined(__linux__)
