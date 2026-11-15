#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/extensions/filters/http/body_size_limit/v3/body_size_limit.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BodySizeLimitFilter {

/**
 * All stats for the body size limit filter. @see stats_macros.h
 */
#define ALL_BODY_SIZE_LIMIT_STATS(COUNTER)                                                         \
  COUNTER(rq_content_length_too_large)                                                             \
  COUNTER(rq_body_too_large)

/**
 * Wrapper struct for body size limit filter stats. @see stats_macros.h
 */
struct BodySizeLimitStats {
  ALL_BODY_SIZE_LIMIT_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the body size limit filter.
 */
class BodySizeLimitFilterConfig {
public:
  BodySizeLimitFilterConfig(
      const envoy::extensions::filters::http::body_size_limit::v3::BodySizeLimit& proto_config,
      Stats::Scope& scope, const std::string& stats_prefix);

  uint64_t maxRequestBytes() const { return max_request_bytes_; }
  BodySizeLimitStats& stats() { return stats_; }

private:
  const uint64_t max_request_bytes_;
  BodySizeLimitStats stats_;
};

using BodySizeLimitFilterConfigSharedPtr = std::shared_ptr<BodySizeLimitFilterConfig>;

/**
 * A streaming filter that rejects requests exceeding a configured body size limit
 * without buffering the request body.
 */
class BodySizeLimitFilter : public Http::PassThroughDecoderFilter,
                            public Logger::Loggable<Logger::Id::http> {
public:
  BodySizeLimitFilter(BodySizeLimitFilterConfigSharedPtr config);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

private:
  void sizeExceeded(uint64_t length, const char* log_message, const char* reply_text);

  BodySizeLimitFilterConfigSharedPtr config_;
  uint64_t bytes_received_{};
};

} // namespace BodySizeLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
