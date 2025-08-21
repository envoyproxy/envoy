#include "source/extensions/quic/connection_debug_visitor/quic_stats/quic_stats.h"

#include <memory>

#include "envoy/stream_info/stream_info.h"

#include "source/common/common/logger.h"
#include "source/common/quic/envoy_quic_connection_debug_visitor_factory_interface.h"

#include "quiche/quic/core/frames/quic_connection_close_frame.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_types.h"

namespace Envoy {
namespace Extensions {
namespace Quic {
namespace ConnectionDebugVisitors {
namespace QuicStats {

QuicStatsVisitor::QuicStatsVisitor(Config& config, Event::Dispatcher& dispatcher)
    : config_(config) {
  if (config_.update_period_.has_value()) {
    timer_ = dispatcher.createTimer([this]() {
      recordStats();
      timer_->enableTimer(config_.update_period_.value());
    });
    timer_->enableTimer(config_.update_period_.value());
  }
}

void QuicStatsVisitor::OnConnectionClosed(const quic::QuicConnectionCloseFrame&,
                                          quic::ConnectionCloseSource) {
  if (timer_ != nullptr) {
    timer_->disableTimer();
  }
  recordStats();
}

const quic::QuicConnectionStats& QuicStatsVisitorProd::getQuicStats() {
  return session_.connection()->GetStats();
}

void QuicStatsVisitor::recordStats() {
  const quic::QuicConnectionStats& quic_stats = getQuicStats();

  auto update_counter = [](Stats::Counter& counter, auto& last_value, auto current_value) {
    int64_t diff = static_cast<int64_t>(current_value) - static_cast<int64_t>(last_value);
    ASSERT(diff >= 0);
    if (diff > 0) {
      counter.add(diff);
    }
    last_value = current_value;
  };

  // This is before the update to `last_packets_sent_` and `last_packets_retransmitted_` because
  // they use the same metrics, and `update_counter` will update `last_...`, so this needs to use
  // those `last_...` values (and not update them) first.
  //
  // Don't record a value if the numerator is negative, or the denominator is zero or negative
  // (prevent divide-by-zero).
  if ((quic_stats.packets_sent > last_packets_sent_) &&
      (quic_stats.packets_retransmitted >= last_packets_retransmitted_)) {
    const uint64_t pkts_diff = quic_stats.packets_sent - last_packets_sent_;
    const uint64_t retrans_diff = quic_stats.packets_retransmitted - last_packets_retransmitted_;

    // The following math will not overflow unless the number of packets seen is well over 10
    // trillion. In that case, the value recorded in the histogram may be incorrect. This case is
    // left unhandled for performance, and it is extremely unlikely.
    constexpr uint64_t max_supported = UINT64_MAX / Stats::Histogram::PercentScale;
    constexpr uint64_t ten_trillion = (10ULL * 1000ULL * 1000ULL * 1000ULL * 1000ULL);
    static_assert(max_supported > ten_trillion);
    ASSERT(retrans_diff < max_supported);

    const uint64_t percent_retransmissions =
        (retrans_diff * static_cast<uint64_t>(Stats::Histogram::PercentScale)) / pkts_diff;
    config_.stats_.cx_tx_percent_retransmitted_packets_.recordValue(percent_retransmissions);
  }

  update_counter(config_.stats_.cx_tx_packets_total_, last_packets_sent_, quic_stats.packets_sent);
  update_counter(config_.stats_.cx_tx_packets_retransmitted_total_, last_packets_retransmitted_,
                 quic_stats.packets_retransmitted);
  update_counter(config_.stats_.cx_tx_amplification_throttling_total_,
                 last_num_amplification_throttling_, quic_stats.num_amplification_throttling);
  update_counter(config_.stats_.cx_rx_packets_total_, last_packets_received_,
                 quic_stats.packets_received);
  update_counter(config_.stats_.cx_path_degrading_total_, last_num_path_degrading_,
                 quic_stats.num_path_degrading);
  update_counter(config_.stats_.cx_forward_progress_after_path_degrading_total_,
                 last_num_forward_progress_after_path_degrading_,
                 quic_stats.num_forward_progress_after_path_degrading);

  if (quic_stats.srtt_us > 0) {
    config_.stats_.cx_rtt_us_.recordValue(quic_stats.srtt_us);
  }
  if (!quic_stats.estimated_bandwidth.IsZero() && !quic_stats.estimated_bandwidth.IsInfinite()) {
    config_.stats_.cx_tx_estimated_bandwidth_.recordValue(
        quic_stats.estimated_bandwidth.ToBytesPerPeriod(quic::QuicTime::Delta::FromSeconds(1)));
  }
  config_.stats_.cx_tx_mtu_.recordValue(quic_stats.egress_mtu);
  config_.stats_.cx_rx_mtu_.recordValue(quic_stats.ingress_mtu);
}

Config::Config(
    const envoy::extensions::quic::connection_debug_visitor::quic_stats::v3::Config& config,
    Stats::Scope& scope)
    : update_period_(PROTOBUF_GET_OPTIONAL_MS(config, update_period)),
      stats_(generateStats(scope)) {}

QuicStats Config::generateStats(Stats::Scope& scope) {
  constexpr absl::string_view prefix("quic_stats");
  return QuicStats{ALL_QUIC_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                  POOL_GAUGE_PREFIX(scope, prefix),
                                  POOL_HISTOGRAM_PREFIX(scope, prefix))};
}

std::unique_ptr<quic::QuicConnectionDebugVisitor>
Config::createQuicConnectionDebugVisitor(Event::Dispatcher& dispatcher, quic::QuicSession& session,
                                         const StreamInfo::StreamInfo&) {
  return std::make_unique<QuicStatsVisitorProd>(*this, dispatcher, session);
}

Envoy::Quic::EnvoyQuicConnectionDebugVisitorFactoryInterfacePtr
QuicStatsFactoryFactory::createFactory(
    const Protobuf::Message& config,
    Server::Configuration::ListenerFactoryContext& server_context) {
  return std::make_unique<Config>(
      dynamic_cast<
          const envoy::extensions::quic::connection_debug_visitor::quic_stats::v3::Config&>(config),
      server_context.listenerScope());
}

REGISTER_FACTORY(QuicStatsFactoryFactory,
                 Envoy::Quic::EnvoyQuicConnectionDebugVisitorFactoryFactoryInterface);

} // namespace QuicStats
} // namespace ConnectionDebugVisitors
} // namespace Quic
} // namespace Extensions
} // namespace Envoy
