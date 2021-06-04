#include "source/extensions/filters/http/on_demand/on_demand_update.h"

#include "source/common/common/assert.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/logger.h"
#include "source/common/http/codes.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OnDemand {

Http::FilterHeadersStatus OnDemandRouteUpdate::decodeHeaders(Http::RequestHeaderMap&, bool) {

  if (callbacks_->route() != nullptr) {
    filter_iteration_state_ = Http::FilterHeadersStatus::Continue;
    return filter_iteration_state_;
  }
  // decodeHeaders() is interrupted.
  decode_headers_active_ = true;
  route_config_updated_callback_ =
      std::make_shared<Http::RouteConfigUpdatedCallback>(Http::RouteConfigUpdatedCallback(
          [this](bool route_exists) -> void { onRouteConfigUpdateCompletion(route_exists); }));
  filter_iteration_state_ = Http::FilterHeadersStatus::StopIteration;
  callbacks_->requestRouteConfigUpdate(route_config_updated_callback_);
  // decodeHeaders() is completed.
  decode_headers_active_ = false;
  return filter_iteration_state_;
}

Http::FilterDataStatus OnDemandRouteUpdate::decodeData(Buffer::Instance&, bool) {
  return filter_iteration_state_ == Http::FilterHeadersStatus::StopIteration
             ? Http::FilterDataStatus::StopIterationAndWatermark
             : Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus OnDemandRouteUpdate::decodeTrailers(Http::RequestTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void OnDemandRouteUpdate::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

// A weak_ptr copy of the route_config_updated_callback_ is kept by RdsRouteConfigProviderImpl
// in config_update_callbacks_. By resetting the pointer in onDestroy() callback we ensure
// that this filter/filter-chain will not be resumed if the corresponding has been closed
void OnDemandRouteUpdate::onDestroy() { route_config_updated_callback_.reset(); }

// This is the callback which is called when an update requested in requestRouteConfigUpdate()
// has been propagated to workers, at which point the request processing is restarted from the
// beginning.
void OnDemandRouteUpdate::onRouteConfigUpdateCompletion(bool route_exists) {
  filter_iteration_state_ = Http::FilterHeadersStatus::Continue;

  // Don't call continueDecoding in the middle of decodeHeaders()
  if (decode_headers_active_) {
    return;
  }

  if (route_exists &&                  // route can be resolved after an on-demand
                                       // VHDS update
      !callbacks_->decodingBuffer() && // Redirects with body not yet supported.
      callbacks_->recreateStream(/*headers=*/nullptr)) {
    return;
  }

  // route cannot be resolved after an on-demand VHDS update or
  // recreating stream failed, continue the filter-chain
  callbacks_->continueDecoding();
}

} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
