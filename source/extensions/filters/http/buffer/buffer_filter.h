#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/filter/http/buffer/v2/buffer.pb.h"
#include "envoy/http/filter.h"
#include "envoy/stats/scope.h"
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

class BufferFilterSettings : public Router::RouteSpecificFilterConfig {
public:
  BufferFilterSettings(const envoy::config::filter::http::buffer::v2::Buffer&);
  BufferFilterSettings(const envoy::config::filter::http::buffer::v2::BufferPerRoute&);

  bool disabled() const { return disabled_; }
  uint64_t maxRequestBytes() const { return max_request_bytes_; }
  std::chrono::seconds maxRequestTime() const { return max_request_time_; }

private:
  bool disabled_;
  uint64_t max_request_bytes_;
  std::chrono::seconds max_request_time_;
};

/**
 * Configuration for the buffer filter.
 */
class BufferFilterConfig {
public:
  BufferFilterConfig(const envoy::config::filter::http::buffer::v2::Buffer& proto_config,
                     const std::string& stats_prefix, Stats::Scope& scope);

  BufferFilterStats& stats() { return stats_; }
  const BufferFilterSettings* settings() const { return &settings_; }

private:
  BufferFilterStats stats_;
  const BufferFilterSettings settings_;
};

typedef std::shared_ptr<BufferFilterConfig> BufferFilterConfigSharedPtr;

/**
 * A filter that is capable of buffering an entire request before dispatching it upstream.
 */
class BufferFilter : public Http::StreamDecoderFilter {
public:
  BufferFilter(BufferFilterConfigSharedPtr config);
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
  void initConfig();

  BufferFilterConfigSharedPtr config_;
  const BufferFilterSettings* settings_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  Event::TimerPtr request_timeout_;
  bool config_initialized_{};
};

} // namespace BufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
