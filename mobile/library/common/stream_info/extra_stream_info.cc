#include "library/common/stream_info/extra_stream_info.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace StreamInfo {
namespace {

void setFromOptional(int64_t& to_set, const absl::optional<MonotonicTime>& time) {
  if (time.has_value()) {
    to_set = std::chrono::duration_cast<std::chrono::milliseconds>(time.value().time_since_epoch())
                 .count();
  }
}

void setFromOptional(int64_t& to_set, absl::optional<std::chrono::nanoseconds> time, long offset) {
  if (time.has_value()) {
    to_set = offset + std::chrono::duration_cast<std::chrono::milliseconds>(time.value()).count();
  }
}

} // namespace

const std::string& ExtraStreamInfo::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy_mobile.extra_stream_info");
}

void setFinalStreamIntel(StreamInfo& stream_info, envoy_final_stream_intel& final_intel) {
  if (stream_info.upstreamInfo()) {
    const auto& upstream_info = stream_info.upstreamInfo();
    const UpstreamTiming& timing = upstream_info->upstreamTiming();
    setFromOptional(final_intel.sending_start_ms, timing.first_upstream_tx_byte_sent_);
    setFromOptional(final_intel.sending_end_ms, timing.last_upstream_tx_byte_sent_);
    setFromOptional(final_intel.response_start_ms, timing.first_upstream_rx_byte_received_);
    setFromOptional(final_intel.connect_start_ms, timing.upstream_connect_start_);
    setFromOptional(final_intel.connect_end_ms, timing.upstream_connect_complete_);
    if (timing.upstream_handshake_complete_.has_value()) {
      setFromOptional(final_intel.ssl_start_ms, timing.upstream_connect_complete_);
    }
    setFromOptional(final_intel.ssl_end_ms, timing.upstream_handshake_complete_);
    final_intel.socket_reused = upstream_info->upstreamNumStreams() > 1;
  }
  final_intel.request_start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     stream_info.startTimeMonotonic().time_since_epoch())
                                     .count();
  TimingUtility timing(stream_info);
  setFromOptional(final_intel.request_end_ms, timing.lastDownstreamRxByteReceived(),
                  final_intel.request_start_ms);
  setFromOptional(final_intel.dns_start_ms, stream_info.downstreamTiming().getValue(
                                                "envoy.dynamic_forward_proxy.dns_start_ms"));
  setFromOptional(final_intel.dns_end_ms, stream_info.downstreamTiming().getValue(
                                              "envoy.dynamic_forward_proxy.dns_end_ms"));
  if (stream_info.getUpstreamBytesMeter()) {
    final_intel.sent_byte_count = stream_info.getUpstreamBytesMeter()->wireBytesSent();
    final_intel.received_byte_count = stream_info.getUpstreamBytesMeter()->wireBytesReceived();
  }
  final_intel.response_flags = stream_info.responseFlags();
}

} // namespace StreamInfo
} // namespace Envoy
