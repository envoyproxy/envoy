#pragma once

#include <cstdint>

#include "envoy/access_log/access_log.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/runtime/runtime_features.h"

#include "quiche/quic/core/quic_ack_listener_interface.h"
#include "quiche/quic/platform/api/quic_flags.h"

namespace Envoy {
namespace Quic {

// Ack listener that stores access logging information and performs
// logging after the final ack.
class QuicStatsGatherer : public quic::QuicAckListenerInterface {
public:
  explicit QuicStatsGatherer(Envoy::TimeSource* time_source) : time_source_(time_source) {}
  ~QuicStatsGatherer() override {
    if (!logging_done_) {
      if (notify_ack_listener_before_soon_to_be_destroyed_) {
        ENVOY_BUG(stream_info_ == nullptr,
                  "Stream destroyed without logging metrics available in stream info.");
      } else {
        maybeDoDeferredLog(false);
      }
    }
  }

  // QuicAckListenerInterface
  void OnPacketAcked(int acked_bytes, quic::QuicTime::Delta delta_largest_observed) override;
  void OnPacketRetransmitted(int retransmitted_bytes) override;

  // Add bytes sent for this stream, for internal tracking of bytes acked.
  void addBytesSent(uint64_t bytes_sent, bool end_stream) {
    bytes_outstanding_ += bytes_sent;
    fin_sent_ = end_stream;
  }
  // Log this stream using available stream info and access loggers.
  void maybeDoDeferredLog(bool record_ack_timing = true);
  // Set list of pointers to access loggers.
  void setAccessLogHandlers(AccessLog::InstanceSharedPtrVector handlers) {
    access_log_handlers_ = std::move(handlers);
  }
  // Set headers, trailers, and stream info used for deferred logging.
  void
  setDeferredLoggingHeadersAndTrailers(Http::RequestHeaderMapConstSharedPtr request_header_map,
                                       Http::ResponseHeaderMapConstSharedPtr response_header_map,
                                       Http::ResponseTrailerMapConstSharedPtr response_trailer_map,
                                       std::unique_ptr<StreamInfo::StreamInfo> stream_info) {
    request_header_map_ = request_header_map;
    response_header_map_ = response_header_map;
    response_trailer_map_ = response_trailer_map;
    stream_info_ = std::move(stream_info);
  }
  bool loggingDone() { return logging_done_; }
  uint64_t bytesOutstanding() { return bytes_outstanding_; }
  bool notify_ack_listener_before_soon_to_be_destroyed() const {
    return notify_ack_listener_before_soon_to_be_destroyed_;
  }

private:
  uint64_t bytes_outstanding_ = 0;
  bool fin_sent_ = false;
  AccessLog::InstanceSharedPtrVector access_log_handlers_{};
  Http::RequestHeaderMapConstSharedPtr request_header_map_;
  Http::ResponseHeaderMapConstSharedPtr response_header_map_;
  Http::ResponseTrailerMapConstSharedPtr response_trailer_map_;
  // nullptr indicates that deferred logging should be skipped.
  std::unique_ptr<StreamInfo::StreamInfo> stream_info_;
  Envoy::TimeSource* time_source_ = nullptr;
  bool logging_done_ = false;
  uint64_t retransmitted_packets_ = 0;
  uint64_t retransmitted_bytes_ = 0;
  absl::optional<MonotonicTime> last_downstream_ack_timestamp_;

  const bool notify_ack_listener_before_soon_to_be_destroyed_{
      GetQuicReloadableFlag(quic_notify_ack_listener_earlier) &&
      GetQuicReloadableFlag(quic_notify_stream_soon_to_destroy)};
  const bool fix_defer_logging_miss_for_half_closed_stream_{Runtime::runtimeFeatureEnabled(
      "envoy.reloadable_features.quic_fix_defer_logging_miss_for_half_closed_stream")};
};

} // namespace Quic
} // namespace Envoy
