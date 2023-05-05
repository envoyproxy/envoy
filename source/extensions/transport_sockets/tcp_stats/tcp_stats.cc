#if defined(__linux__)

// `struct tcp_info` is defined in two places: /usr/include/netinet/tcp.h (included from
// envoy/common/platform.h) and /usr/include/linux/tcp.h. The former version is older and doesn't
// contain all the fields needed. Including both headers results in a compilation error due to the
// duplicate (and different) definitions of `struct tcp_info`. To work around this, define
// `DO_NOT_INCLUDE_NETINET_TCP_H` to prevent inclusion of the wrong version.
#define DO_NOT_INCLUDE_NETINET_TCP_H 1

#include "source/extensions/transport_sockets/tcp_stats/tcp_stats.h"

#include <linux/tcp.h>

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

TcpStats Config::generateStats(Stats::Scope& scope) {
  const std::string prefix("tcp_stats");
  return TcpStats{ALL_TCP_STATS(POOL_COUNTER_PREFIX(scope, prefix),
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
      recordStats();
      timer_->enableTimer(config_->update_period_.value());
    });
    timer_->enableTimer(config_->update_period_.value());
  }

  transport_socket_->onConnected();
}

void TcpStatsSocket::closeSocket(Network::ConnectionEvent event) {
  // Record final values.
  recordStats();

  // Ensure gauges are zero'd out at the end of a connection no matter what the OS told us.
  if (last_cx_tx_unsent_bytes_ > 0) {
    config_->stats_.cx_tx_unsent_bytes_.sub(last_cx_tx_unsent_bytes_);
  }
  if (last_cx_tx_unacked_segments_ > 0) {
    config_->stats_.cx_tx_unacked_segments_.sub(last_cx_tx_unacked_segments_);
  }

  if (timer_ != nullptr) {
    timer_->disableTimer();
  }

  transport_socket_->closeSocket(event);
}

absl::optional<struct tcp_info> TcpStatsSocket::querySocketInfo() {
  struct tcp_info info;
  memset(&info, 0, sizeof(info));
  socklen_t optlen = sizeof(info);
  const auto result = callbacks_->ioHandle().getOption(IPPROTO_TCP, TCP_INFO, &info, &optlen);
  if (result.return_value_ != 0) {
    ENVOY_LOG(debug, "Failed getsockopt(IPPROTO_TCP, TCP_INFO): rc {} errno {} optlen {}",
              result.return_value_, result.errno_, optlen);
    return absl::nullopt;
  } else {
    return info;
  }
}

void TcpStatsSocket::recordStats() {
  absl::optional<struct tcp_info> tcp_info = querySocketInfo();
  if (!tcp_info.has_value()) {
    return;
  }

  auto update_counter = [](Stats::Counter& counter, auto& last_value, auto current_value) {
    int64_t diff = static_cast<int64_t>(current_value) - static_cast<int64_t>(last_value);
    ASSERT(diff >= 0);
    if (diff > 0) {
      counter.add(diff);
    }
    last_value = current_value;
  };

  auto update_gauge = [](Stats::Gauge& gauge, auto& last_value, auto current_value) {
    static_assert(sizeof(last_value) == sizeof(current_value));
    int64_t diff = static_cast<int64_t>(current_value) - static_cast<int64_t>(last_value);
    gauge.add(diff);
    last_value = current_value;
  };

  // This is before the update to `cx_tx_data_segments_` and `cx_tx_retransmitted_segments_` because
  // they use the same metrics, and `update_counter` will update `last_...`, so this needs to use
  // those `last_...` values (and not update them) first.
  //
  // Don't record a value if the numerator is negative, or the denominator is zero or negative
  // (prevent divide-by-zero).
  if ((tcp_info->tcpi_data_segs_out > last_cx_tx_data_segments_) &&
      (tcp_info->tcpi_total_retrans >= last_cx_tx_retransmitted_segments_)) {
    // uint32 * uint32 cannot overflow a uint64, so this can safely be done as integer math
    // instead of floating point.
    static_assert((sizeof(tcp_info->tcpi_total_retrans) == sizeof(uint32_t)) &&
                  (Stats::Histogram::PercentScale < UINT32_MAX));

    const uint32_t data_segs_out_diff = tcp_info->tcpi_data_segs_out - last_cx_tx_data_segments_;
    const uint32_t retransmitted_segs_diff =
        tcp_info->tcpi_total_retrans - last_cx_tx_retransmitted_segments_;
    const uint64_t percent_retransmissions =
        (static_cast<uint64_t>(retransmitted_segs_diff) *
         static_cast<uint64_t>(Stats::Histogram::PercentScale)) /
        static_cast<uint64_t>(data_segs_out_diff);
    config_->stats_.cx_tx_percent_retransmitted_segments_.recordValue(percent_retransmissions);
  }

  const uint64_t mss = (tcp_info->tcpi_snd_mss > 0) ? tcp_info->tcpi_snd_mss : 1460;
  // Convert packets to bytes.
  auto snd_cwnd = tcp_info->tcpi_snd_cwnd * mss;
  // Convert delivery & pacing rate to MB.
  auto delivery_rate_mb = std::round(static_cast<float>(tcp_info->tcpi_delivery_rate) / 1000000);
  auto pacing_rate_mb = std::round(static_cast<float>(tcp_info->tcpi_pacing_rate) / 1000000);

  update_counter(config_->stats_.cx_tx_segments_, last_cx_tx_segments_, tcp_info->tcpi_segs_out);
  update_counter(config_->stats_.cx_rx_segments_, last_cx_rx_segments_, tcp_info->tcpi_segs_in);
  update_counter(config_->stats_.cx_tx_data_segments_, last_cx_tx_data_segments_,
                 tcp_info->tcpi_data_segs_out);
  update_counter(config_->stats_.cx_rx_data_segments_, last_cx_rx_data_segments_,
                 tcp_info->tcpi_data_segs_in);
  update_counter(config_->stats_.cx_tx_retransmitted_segments_, last_cx_tx_retransmitted_segments_,
                 tcp_info->tcpi_total_retrans);
  update_counter(config_->stats_.cx_rx_bytes_received_, last_cx_rx_bytes_received_,
                 tcp_info->tcpi_bytes_received);
  update_counter(config_->stats_.cx_tx_bytes_sent_, last_cx_tx_bytes_sent_,
                 tcp_info->tcpi_bytes_sent);
  update_counter(config_->stats_.cx_data_segments_delivered_, last_cx_data_segments_delivered_,
                 tcp_info->tcpi_delivered);
  update_counter(config_->stats_.cx_reordering_, last_cx_reordering_, tcp_info->tcpi_reordering);

  update_gauge(config_->stats_.cx_tx_unsent_bytes_, last_cx_tx_unsent_bytes_,
               tcp_info->tcpi_notsent_bytes);
  update_gauge(config_->stats_.cx_tx_unacked_segments_, last_cx_tx_unacked_segments_,
               tcp_info->tcpi_unacked);
  update_gauge(config_->stats_.cx_rto_us_, last_cx_rto_us_, tcp_info->tcpi_rto);
  update_gauge(config_->stats_.cx_ato_us_, last_cx_ato_us_, tcp_info->tcpi_ato);
  update_gauge(config_->stats_.cx_lost_, last_cx_lost_, tcp_info->tcpi_lost);
  update_gauge(config_->stats_.cx_tx_ssthreshold_, last_cx_tx_ssthreshold_,
               tcp_info->tcpi_snd_ssthresh);
  update_gauge(config_->stats_.cx_rx_ssthreshold_, last_cx_rx_ssthreshold_,
               tcp_info->tcpi_rcv_ssthresh);
  update_gauge(config_->stats_.cx_tx_mss_bytes_, last_cx_tx_mss_bytes_, tcp_info->tcpi_snd_mss);
  update_gauge(config_->stats_.cx_rx_mss_bytes_, last_cx_rx_mss_bytes_, tcp_info->tcpi_rcv_mss);
  update_gauge(config_->stats_.cx_advmss_bytes_, last_cx_advmss_bytes_, tcp_info->tcpi_advmss);
  update_gauge(config_->stats_.cx_pmtu_bytes_, last_cx_pmtu_bytes_, tcp_info->tcpi_pmtu);

  config_->stats_.cx_rtt_us_.recordValue(tcp_info->tcpi_rtt);
  config_->stats_.cx_rtt_variance_us_.recordValue(tcp_info->tcpi_rttvar);
  config_->stats_.cx_congestion_window_.recordValue(snd_cwnd);
  config_->stats_.cx_pacing_rate_.recordValue(pacing_rate_mb);
  config_->stats_.cx_delivery_rate_.recordValue(delivery_rate_mb);
  config_->stats_.cx_rcv_rtt_.recordValue(tcp_info->tcpi_rcv_rtt);
  config_->stats_.cx_tx_window_scale_.recordValue(tcp_info->tcpi_snd_wscale);
  config_->stats_.cx_rx_window_scale_.recordValue(tcp_info->tcpi_rcv_wscale);

  Envoy::ProtobufWkt::Struct meta_struct;
  auto& fields = *meta_struct.mutable_fields();
  *fields["cx_tx_segments"].mutable_string_value() = absl::StrCat(tcp_info->tcpi_segs_out);
  *fields["cx_rx_segments"].mutable_string_value() = absl::StrCat(tcp_info->tcpi_segs_in);
  *fields["cx_tx_data_segments"].mutable_string_value() =
      absl::StrCat(tcp_info->tcpi_data_segs_out);
  *fields["cx_rx_data_segments"].mutable_string_value() = absl::StrCat(tcp_info->tcpi_data_segs_in);
  *fields["cx_tx_retransmitted_segments"].mutable_string_value() =
      absl::StrCat(tcp_info->tcpi_total_retrans);
  *fields["cx_rx_bytes_received"].mutable_string_value() =
      absl::StrCat(tcp_info->tcpi_bytes_received);
  *fields["cx_tx_bytes_sent"].mutable_string_value() = absl::StrCat(tcp_info->tcpi_bytes_sent);
  *fields["cx_tx_unsent_bytes"].mutable_string_value() = absl::StrCat(tcp_info->tcpi_notsent_bytes);
  *fields["cx_tx_unacked_segments"].mutable_string_value() = absl::StrCat(tcp_info->tcpi_unacked);
  *fields["cx_data_segments_delivered"].mutable_string_value() =
      absl::StrCat(tcp_info->tcpi_delivered);
  *fields["cx_reordering"].mutable_string_value() = absl::StrCat(tcp_info->tcpi_reordering);
  *fields["cx_rto_us"].mutable_string_value() = absl::StrCat(tcp_info->tcpi_rto);
  *fields["cx_ato_us"].mutable_string_value() = absl::StrCat(tcp_info->tcpi_ato);
  *fields["cx_lost"].mutable_string_value() = absl::StrCat(tcp_info->tcpi_lost);
  *fields["cx_tx_ssthreshold"].mutable_string_value() = absl::StrCat(tcp_info->tcpi_snd_ssthresh);
  *fields["cx_rx_ssthreshold"].mutable_string_value() = absl::StrCat(tcp_info->tcpi_rcv_ssthresh);
  *fields["cx_tx_mss_bytes"].mutable_string_value() = absl::StrCat(tcp_info->tcpi_snd_mss);
  *fields["cx_rx_mss_bytes"].mutable_string_value() = absl::StrCat(tcp_info->tcpi_rcv_mss);
  *fields["cx_advmss_bytes"].mutable_string_value() = absl::StrCat(tcp_info->tcpi_advmss);
  *fields["cx_pmtu_bytes"].mutable_string_value() = absl::StrCat(tcp_info->tcpi_pmtu);
  *fields["cx_rtt_us"].mutable_string_value() = absl::StrCat(tcp_info->tcpi_rtt);
  *fields["cx_rtt_variance_us"].mutable_string_value() = absl::StrCat(tcp_info->tcpi_rttvar);
  *fields["cx_rcv_rtt"].mutable_string_value() = absl::StrCat(tcp_info->tcpi_rcv_rtt);
  *fields["cx_tx_window_scale"].mutable_string_value() = absl::StrCat(tcp_info->tcpi_snd_wscale);
  *fields["cx_rx_window_scale"].mutable_string_value() = absl::StrCat(tcp_info->tcpi_rcv_wscale);
  *fields["cx_congestion_window"].mutable_string_value() = absl::StrCat(snd_cwnd);
  *fields["cx_pacing_rate"].mutable_string_value() = absl::StrCat(pacing_rate_mb);
  *fields["cx_delivery_rate"].mutable_string_value() = absl::StrCat(delivery_rate_mb);

  // Update dynamic metadata with tcp stats.
  callbacks_->connection().streamInfo().setDynamicMetadata("envoy.tcp_stats", meta_struct);
}

} // namespace TcpStats
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
#endif // defined(__linux__)
