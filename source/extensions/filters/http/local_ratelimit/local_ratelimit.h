#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/local_ratelimit/v3/local_rate_limit.pb.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/http/header_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalRateLimitFilter {

/**
 * Global configuration for the HTTP local rate limit filter.
 */
class FilterConfig : public ::Envoy::Router::RouteSpecificFilterConfig {
public:
  FilterConfig(const envoy::extensions::filters::http::local_ratelimit::v3::LocalRateLimit&,
               Runtime::Loader& runtime)
      : runtime_(runtime) {}
  Runtime::Loader& runtime() { return runtime_; }

private:
  Runtime::Loader& runtime_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * HTTP local rate limit filter. Depending on the route configuration, this filter calls consults
 * with local token bucket before allowing further filter iteration.
 */
class Filter : public Http::StreamFilter {
public:
  Filter(FilterConfigSharedPtr config) : config_(config) {}

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::ResponseHeaderMap& headers) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;

private:
  FilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
};

} // namespace LocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
