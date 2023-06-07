#include "source/common/quic/quic_stats_gatherer.h"

#include <cstdint>

namespace Envoy {
namespace Quic {

void QuicStatsGatherer::OnPacketAcked(int acked_bytes,
                                      quic::QuicTime::Delta /* delta_largest_observed */) {
  bytes_outstanding_ -= acked_bytes;
  if (bytes_outstanding_ == 0 && fin_sent_ && !logging_done_) {
    maybeDoDeferredLog();
  }
}

void QuicStatsGatherer::OnPacketRetransmitted(int retransmitted_bytes) {
  retransmitted_packets_++;
  retransmitted_bytes_ += retransmitted_bytes;
}

void QuicStatsGatherer::maybeDoDeferredLog(bool record_ack_timing) {
  logging_done_ = true;
  if (stream_info_ == nullptr) {
    return;
  }
  if (time_source_ != nullptr && record_ack_timing) {
    stream_info_->downstreamTiming().onLastDownstreamAckReceived(*time_source_);
  }
  stream_info_->addBytesRetransmitted(retransmitted_bytes_);
  stream_info_->addPacketsRetransmitted(retransmitted_packets_);
  const Http::RequestHeaderMap* request_headers = request_header_map_.get();
  const Http::ResponseHeaderMap* response_headers = response_header_map_.get();
  const Http::ResponseTrailerMap* response_trailers = response_trailer_map_.get();
  for (const AccessLog::InstanceSharedPtr& log_handler : access_log_handlers_) {
    log_handler->log(request_headers, response_headers, response_trailers, *stream_info_,
                     AccessLog::AccessLogType::DownstreamEnd);
  }
}

} // namespace Quic
} // namespace Envoy
