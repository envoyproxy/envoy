#pragma once

#include "envoy/http/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OnDemand {

class OnDemandRouteUpdate : public Http::StreamDecoderFilter {
public:
  OnDemandRouteUpdate() = default;

  Envoy::Http::FilterHeadersStatus requestRouteConfigUpdate();

  void onRouteConfigUpdateCompletion();

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap& trailers) override;

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  void onDestroy() override {}

  void notify() override;

private:
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  Envoy::Http::FilterHeadersStatus filter_iteration_state_{Http::FilterHeadersStatus::Continue};
};

} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
