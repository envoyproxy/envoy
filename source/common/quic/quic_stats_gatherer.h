#pragma once

#include <cstdint>

#include "envoy/access_log/access_log.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

#include "quiche/quic/core/quic_ack_listener_interface.h"

namespace Envoy {
namespace Quic {

// Ack listener that stores access logging information and performs
// logging after the final ack.
class QuicStatsGatherer : public quic::QuicAckListenerInterface {
public:
  explicit QuicStatsGatherer(Envoy::TimeSource* time_source) : time_source_(time_source) {}

  // QuicAckListenerInterface
  void OnPacketAcked(int acked_bytes, quic::QuicTime::Delta delta_largest_observed) override;
  void OnPacketRetransmitted(int /* retransmitted_bytes */) override {}

  // Add bytes sent for this stream, for internal tracking of bytes acked.
  void addBytesSent(uint64_t bytes_sent, bool end_stream) {
    bytes_outstanding_ += bytes_sent;
    fin_sent_ = end_stream;
  }
  // Log this stream using available stream info and access loggers.
  void maybeDoDeferredLog(bool record_ack_timing = true);
  // Set list of pointers to access loggers.
  void setAccessLogHandlers(std::list<AccessLog::InstanceSharedPtr> handlers) {
    access_log_handlers_ = handlers;
  }
  // Set headers and trailers used for deferred logging.
  void setDeferredLoggingHeadersAndTrailers(
      Http::DeferredLoggingHeadersAndTrailers& headers_and_trailers) {
    deferred_logging_headers_and_trailers_ =
        absl::make_optional<Http::DeferredLoggingHeadersAndTrailers>(
            std::move(headers_and_trailers));
  }
  bool loggingDone() { return logging_done_; }
  uint64_t bytesOutstanding() { return bytes_outstanding_; }

private:
  uint64_t bytes_outstanding_ = 0;
  bool fin_sent_ = false;
  std::list<AccessLog::InstanceSharedPtr> access_log_handlers_{};
  // Headers and trailers required for deferred logging. nullopt indicates that deferred logging
  // should be skipped.
  absl::optional<Http::DeferredLoggingHeadersAndTrailers> deferred_logging_headers_and_trailers_;
  Envoy::TimeSource* time_source_ = nullptr;
  bool logging_done_ = false;
};

} // namespace Quic
} // namespace Envoy
