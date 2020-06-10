#pragma once

#include "envoy/http/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OnDemand {

class OnDemandRouteUpdate : public Http::StreamDecoderFilter {
public:
  OnDemandRouteUpdate() = default;

  void onRouteConfigUpdateCompletion(bool route_exists);

  void setFilterIterationState(Envoy::Http::FilterHeadersStatus status) {
    filter_iteration_state_ = status;
  }

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  void onDestroy() override;

private:
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_callback_;
  Envoy::Http::FilterHeadersStatus filter_iteration_state_{Http::FilterHeadersStatus::Continue};
};

} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
