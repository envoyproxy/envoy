#pragma once

#include <cstdint>
#include <memory>

#include "envoy/extensions/filters/http/body_size_limit/v3/body_size_limit.pb.h"
#include "envoy/http/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BodySizeLimitFilter {

/**
 * Configuration for the body size limit filter.
 */
class BodySizeLimitFilterConfig {
public:
  BodySizeLimitFilterConfig(
      const envoy::extensions::filters::http::body_size_limit::v3::BodySizeLimit& proto_config);

  uint64_t maxRequestBytes() const { return max_request_bytes_; }

private:
  const uint64_t max_request_bytes_;
};

using BodySizeLimitFilterConfigSharedPtr = std::shared_ptr<BodySizeLimitFilterConfig>;

/**
 * A streaming filter that rejects requests exceeding a configured body size limit
 * without buffering the request body.
 */
class BodySizeLimitFilter : public Http::StreamDecoderFilter,
                            public Logger::Loggable<Logger::Id::http> {
public:
  BodySizeLimitFilter(BodySizeLimitFilterConfigSharedPtr config);

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

private:
  void sizeExceeded(uint64_t length, const char* logMessage, const char* replyText);

  BodySizeLimitFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  uint64_t bytes_received_{};
};

} // namespace BodySizeLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
