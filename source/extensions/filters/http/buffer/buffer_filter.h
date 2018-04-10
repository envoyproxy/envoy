#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/http/filter.h"
#include "envoy/stats/stats_macros.h"

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BufferFilter {

/**
 * All stats for the buffer filter. @see stats_macros.h
 */
// clang-format off
#define ALL_BUFFER_FILTER_STATS(COUNTER)                                                           \
  COUNTER(rq_timeout)
// clang-format on

/**
 * Wrapper struct for buffer filter stats. @see stats_macros.h
 */
struct BufferFilterStats {
  ALL_BUFFER_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the buffer filter.
 */
struct BufferFilterConfig {
  BufferFilterStats stats_;
  uint64_t max_request_bytes_;
  std::chrono::seconds max_request_time_;
};

typedef std::shared_ptr<const BufferFilterConfig> BufferFilterConfigConstSharedPtr;

/**
 * A filter that is capable of buffering an entire request before dispatching it upstream.
 */
class BufferFilter : public Http::StreamDecoderFilter {
public:
  BufferFilter(BufferFilterConfigConstSharedPtr config);
  ~BufferFilter();

  static BufferFilterStats generateStats(const std::string& prefix, Stats::Scope& scope);

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

private:
  void onRequestTimeout();
  void resetInternalState();

  BufferFilterConfigConstSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  Event::TimerPtr request_timeout_;
  bool stream_destroyed_{};
};

} // namespace BufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
