#include "test/common/http/filters/route_cache_reset/filter.h"

#include "envoy/server/filter_config.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RouteCacheReset {

RouteCacheResetFilter::RouteCacheResetFilter() {}

void RouteCacheResetFilter::onDestroy() {}

Http::FilterHeadersStatus RouteCacheResetFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  callbacks_->downstreamCallbacks()->clearRouteCache();
  ENVOY_LOG(debug, "cleared route cache after decoding headers");
  return Http::FilterHeadersStatus::Continue;
}

void RouteCacheResetFilter::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

Http::FilterDataStatus RouteCacheResetFilter::decodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus RouteCacheResetFilter::decodeTrailers(Http::RequestTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

} // namespace RouteCacheReset
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
