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
  ~QuicStatsGatherer() override {
    if (!logging_done_) {
      maybeDoDeferredLog(false);
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
  void setAccessLogHandlers(std::list<AccessLog::InstanceSharedPtr> handlers) {
    access_log_handlers_ = handlers;
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

private:
  uint64_t bytes_outstanding_ = 0;
  bool fin_sent_ = false;
  std::list<AccessLog::InstanceSharedPtr> access_log_handlers_{};
  Http::RequestHeaderMapConstSharedPtr request_header_map_;
  Http::ResponseHeaderMapConstSharedPtr response_header_map_;
  Http::ResponseTrailerMapConstSharedPtr response_trailer_map_;
  // nullptr indicates that deferred logging should be skipped.
  std::unique_ptr<StreamInfo::StreamInfo> stream_info_;
  Envoy::TimeSource* time_source_ = nullptr;
  bool logging_done_ = false;
  uint64_t retransmitted_packets_ = 0;
  uint64_t retransmitted_bytes_ = 0;
};

} // namespace Quic
} // namespace Envoy
