#pragma once

#if defined(__linux__)

#include "envoy/event/timer.h"
#include "envoy/extensions/transport_sockets/tcp_stats/v3/tcp_stats.pb.h"
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

#define ALL_TCP_STATS(COUNTER, GAUGE, HISTOGRAM)                                                   \
  COUNTER(cx_tx_segments)                                                                          \
  COUNTER(cx_rx_segments)                                                                          \
  COUNTER(cx_tx_data_segments)                                                                     \
  COUNTER(cx_rx_data_segments)                                                                     \
  COUNTER(cx_tx_retransmitted_segments)                                                            \
  COUNTER(cx_rx_bytes_received)                                                                    \
  COUNTER(cx_tx_bytes_sent)                                                                        \
  COUNTER(cx_data_segments_delivered)                                                              \
  COUNTER(cx_reordering)                                                                           \
  GAUGE(cx_tx_unsent_bytes, Accumulate)                                                            \
  GAUGE(cx_tx_unacked_segments, Accumulate)                                                        \
  GAUGE(cx_rto_us, Accumulate)                                                                     \
  GAUGE(cx_ato_us, Accumulate)                                                                     \
  GAUGE(cx_lost, Accumulate)                                                                       \
  GAUGE(cx_tx_ssthreshold, Accumulate)                                                             \
  GAUGE(cx_rx_ssthreshold, Accumulate)                                                             \
  GAUGE(cx_tx_mss_bytes, Accumulate)                                                               \
  GAUGE(cx_rx_mss_bytes, Accumulate)                                                               \
  GAUGE(cx_advmss_bytes, Accumulate)                                                               \
  GAUGE(cx_pmtu_bytes, Accumulate)                                                                 \
  HISTOGRAM(cx_tx_percent_retransmitted_segments, Percent)                                         \
  HISTOGRAM(cx_rtt_us, Microseconds)                                                               \
  HISTOGRAM(cx_rtt_variance_us, Microseconds)                                                      \
  HISTOGRAM(cx_rcv_rtt, Microseconds)                                                              \
  HISTOGRAM(cx_tx_window_scale, Unspecified)                                                       \
  HISTOGRAM(cx_rx_window_scale, Unspecified)                                                       \
  HISTOGRAM(cx_congestion_window, Unspecified)                                                     \
  HISTOGRAM(cx_pacing_rate, Unspecified)                                                           \
  HISTOGRAM(cx_delivery_rate, Unspecified)

struct TcpStats {
  ALL_TCP_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

class Config {
public:
  Config(const envoy::extensions::transport_sockets::tcp_stats::v3::Config& config_proto,
         Stats::Scope& scope);

  TcpStats stats_;
  const absl::optional<std::chrono::milliseconds> update_period_;

private:
  TcpStats generateStats(Stats::Scope& scope);
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
  absl::optional<struct tcp_info> querySocketInfo();
  void recordStats();

  const ConfigConstSharedPtr config_;
  Network::TransportSocketCallbacks* callbacks_{};
  Event::TimerPtr timer_;

  uint32_t last_cx_tx_segments_{};
  uint32_t last_cx_rx_segments_{};
  uint32_t last_cx_tx_data_segments_{};
  uint32_t last_cx_rx_data_segments_{};
  uint32_t last_cx_tx_retransmitted_segments_{};
  uint32_t last_cx_rx_bytes_received_{};
  uint32_t last_cx_tx_bytes_sent_{};
  uint32_t last_cx_tx_unsent_bytes_{};
  uint32_t last_cx_tx_unacked_segments_{};
  uint32_t last_cx_data_segments_delivered_{};
  uint32_t last_cx_reordering_{};
  uint32_t last_cx_rto_us_{};
  uint32_t last_cx_ato_us_{};
  uint32_t last_cx_lost_{};
  uint32_t last_cx_tx_ssthreshold_{};
  uint32_t last_cx_rx_ssthreshold_{};
  uint32_t last_cx_tx_mss_bytes_{};
  uint32_t last_cx_rx_mss_bytes_{};
  uint32_t last_cx_advmss_bytes_{};
  uint32_t last_cx_pmtu_bytes_{};
};

} // namespace TcpStats
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy

#endif // defined(__linux__)
