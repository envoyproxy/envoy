#include "library/common/stream_info/extra_stream_info.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace StreamInfo {
namespace {

void setFromOptional(int64_t& to_set, const absl::optional<MonotonicTime>& time,
                     int64_t offset_ms) {
  if (time.has_value()) {
    to_set = offset_ms +
             std::chrono::duration_cast<std::chrono::milliseconds>(time.value().time_since_epoch())
                 .count();
  }
}

} // namespace

const std::string& ExtraStreamInfo::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy_mobile.extra_stream_info");
}

void setFinalStreamIntel(StreamInfo& stream_info, TimeSource& time_source,
                         envoy_final_stream_intel& final_intel) {
  // The wall clock starting time is the one provided by StreamInfo.startTime(). Its Epoch value in
  // ms goes to final_intel.stream_start_ms directly. This is the only value that was taken from
  // the "wall clock" (a.k.a std::chrono::system_clock:now())
  final_intel.stream_start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    stream_info.startTime().time_since_epoch())
                                    .count();

  // All the following timestamps are monotonic, rebased on the above stream_info.startTime().
  // StreamInfo.startTimeMonotonic() is used to compute the offset for the rebasing. Both
  // StreamInfo.startTimeMonotonic() and StreamInfo.startTime() are a snapshot that was taken
  // at the same time: when invoking the constructor of StreamInfoImpl.
  // NOTE: it worth noticing that here time_since_epoch() does not return the time since epoch.
  //       This is particularly counterintuitive, but that's the usual escape hatch to transform
  //       a duration to a long (int64_t in this case).
  int64_t offset_ms =
      final_intel.stream_start_ms - std::chrono::duration_cast<std::chrono::milliseconds>(
                                        stream_info.startTimeMonotonic().time_since_epoch())
                                        .count();

  // Unfortunately, stream_info.requestComplete() is not set yet.
  final_intel.stream_end_ms = offset_ms + std::chrono::duration_cast<std::chrono::milliseconds>(
                                              time_source.monotonicTime().time_since_epoch())
                                              .count();

  if (stream_info.upstreamInfo()) {
    const auto& upstream_info = stream_info.upstreamInfo();
    const UpstreamTiming& timing = upstream_info->upstreamTiming();
    setFromOptional(final_intel.sending_start_ms, timing.first_upstream_tx_byte_sent_, offset_ms);
    setFromOptional(final_intel.sending_end_ms, timing.last_upstream_tx_byte_sent_, offset_ms);
    setFromOptional(final_intel.response_start_ms, timing.first_upstream_rx_byte_received_,
                    offset_ms);
    setFromOptional(final_intel.connect_start_ms, timing.upstream_connect_start_, offset_ms);
    setFromOptional(final_intel.connect_end_ms, timing.upstream_connect_complete_, offset_ms);
    if (timing.upstream_handshake_complete_.has_value()) {
      setFromOptional(final_intel.ssl_start_ms, timing.upstream_connect_complete_, offset_ms);
    }
    setFromOptional(final_intel.ssl_end_ms, timing.upstream_handshake_complete_, offset_ms);
    final_intel.socket_reused = upstream_info->upstreamNumStreams() > 1;
    if (stream_info.upstreamInfo()->upstreamProtocol().has_value()) {
      final_intel.upstream_protocol =
          static_cast<int64_t>(stream_info.upstreamInfo()->upstreamProtocol().value());
    }
  }

  setFromOptional(
      final_intel.dns_start_ms,
      stream_info.downstreamTiming().getValue("envoy.dynamic_forward_proxy.dns_start_ms"),
      offset_ms);
  setFromOptional(final_intel.dns_end_ms,
                  stream_info.downstreamTiming().getValue("envoy.dynamic_forward_proxy.dns_end_ms"),
                  offset_ms);
  if (stream_info.getUpstreamBytesMeter()) {
    final_intel.sent_byte_count = stream_info.getUpstreamBytesMeter()->wireBytesSent();
    final_intel.received_byte_count = stream_info.getUpstreamBytesMeter()->wireBytesReceived();
  }
  final_intel.response_flags = stream_info.responseFlags();
}

bool isStreamIdleTimeout(const StreamInfo& stream_info) {
  return stream_info.responseCodeDetails().has_value() &&
         stream_info.responseCodeDetails().value() == ResponseCodeDetails::get().StreamIdleTimeout;
}

} // namespace StreamInfo
} // namespace Envoy
