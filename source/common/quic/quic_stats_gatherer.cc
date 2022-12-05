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

void QuicStatsGatherer::maybeDoDeferredLog() {
  logging_done_ = true;
  if (stream_info_ == nullptr) {
    return;
  }
  if (!stream_info_->deferredLoggingHeadersAndTrailers().has_value()) {
    return;
  }
  if (time_source_ != nullptr) {
    stream_info_->downstreamTiming().onLastDownstreamAckReceived(*time_source_);
  }
  auto request_headers =
      stream_info_->deferredLoggingHeadersAndTrailers()->request_header_map.get();
  auto response_headers =
      stream_info_->deferredLoggingHeadersAndTrailers()->response_header_map.get();
  auto response_trailers =
      stream_info_->deferredLoggingHeadersAndTrailers()->response_trailer_map.get();
  for (const auto& log_handler : access_log_handlers_) {
    log_handler->log(request_headers, response_headers, response_trailers, *stream_info_);
  }
}

} // namespace Quic
} // namespace Envoy
