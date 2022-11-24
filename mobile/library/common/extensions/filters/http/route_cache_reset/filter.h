#pragma once

#include "envoy/http/filter.h"

#include "source/common/common/logger.h"

#include "library/common/extensions/filters/http/route_cache_reset/filter.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RouteCacheReset {

/**
 * Filter that has the sole purpose of clearing the route cache for a given
 * stream on the request path. This forces the router filter to recompute
 * routes for outbound requests using information that was potentially
 * mutated by other filters (such as headers).
 */
class RouteCacheResetFilter final : public Http::StreamDecoderFilter,
                                    public Logger::Loggable<Logger::Id::filter> {
public:
  RouteCacheResetFilter();

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

private:
  Http::StreamDecoderFilterCallbacks* callbacks_{};
};

} // namespace RouteCacheReset
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
