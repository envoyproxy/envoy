#pragma once

#if defined(__linux__)

#include "envoy/extensions/filters/network/linux_network_stats/v3/linux_network_stats.pb.h"

#include "envoy/event/timer.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"

// Defined in /usr/include/linux/tcp.h.
struct tcp_info;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LinuxNetworkStats {

#define ALL_LINUX_NETWORK_STATS(COUNTER, GAUGE, HISTOGRAM)                                         \
  COUNTER(cx_tx_segments)                                                                          \
  COUNTER(cx_tx_retransmitted_segments)                                                            \
  GAUGE(cx_tx_unsent_bytes, Accumulate)                                                            \
  HISTOGRAM(cx_tx_percent_retransmitted_segments, Percent)                                         \
  HISTOGRAM(cx_min_rtt_us, Microseconds)

struct LinuxNetworkStats {
  ALL_LINUX_NETWORK_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

class Config {
public:
  Config(const envoy::extensions::filters::network::linux_network_stats::v3::Config& config_proto,
         Stats::Scope& scope);

  LinuxNetworkStats stats_;
  const absl::optional<std::chrono::milliseconds> update_period_;

private:
  LinuxNetworkStats generateStats(const absl::string_view prefix, Stats::Scope& scope);
};

using ConfigConstSharedPtr = std::shared_ptr<const Config>;

class Filter : public Network::ReadFilter,
               public Network::ConnectionCallbacks,
               Logger::Loggable<Logger::Id::filter> {
public:
  Filter(ConfigConstSharedPtr config);

  // Network::ReadFilter
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent) override {}
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}
  void onPreClose() override;

private:
  struct tcp_info querySocketInfo();
  void updateCountersAndGauges(struct tcp_info& tcp_info);
  void recordHistograms(struct tcp_info& tcp_info);

  const ConfigConstSharedPtr config_;
  Network::ReadFilterCallbacks* read_callbacks_{};
  Event::TimerPtr timer_;

  uint64_t last_cx_tx_segments_{};
  uint64_t last_cx_tx_retransmitted_segments_{};
  uint64_t last_cx_tx_unsent_bytes_{};
};

} // namespace LinuxNetworkStats
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

#endif // defined(__linux__)
