#include "source/common/quic/quic_stats_gatherer.h"

#include <cstdint>

#include "envoy/formatter/http_formatter_context.h"

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

  const Formatter::HttpFormatterContext log_context{request_header_map_.get(),
                                                    response_header_map_.get(),
                                                    response_trailer_map_.get(),
                                                    {},
                                                    AccessLog::AccessLogType::DownstreamEnd};

  for (const AccessLog::InstanceSharedPtr& log_handler : access_log_handlers_) {
    log_handler->log(log_context, *stream_info_);
  }
}

} // namespace Quic
} // namespace Envoy
