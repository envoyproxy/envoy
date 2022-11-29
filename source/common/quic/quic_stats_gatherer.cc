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
  for (const std::shared_ptr<StreamInfo::StreamInfo>& stream_info : stream_info_) {
    if (!stream_info->deferredLoggingInfo().has_value()) {
      continue;
    }
    if (time_source_ != nullptr) {
      stream_info->downstreamTiming().onLastDownstreamAckReceived(*time_source_);
    }
    auto request_headers = stream_info->deferredLoggingInfo()->request_header_map.get();
    auto response_headers = stream_info->deferredLoggingInfo()->response_header_map.get();
    auto response_trailers = stream_info->deferredLoggingInfo()->response_trailer_map.get();
    for (const auto& log_handler : access_log_handlers_) {
      log_handler->log(request_headers, response_headers, response_trailers, *stream_info);
    }
  }
}

} // namespace Quic
} // namespace Envoy
