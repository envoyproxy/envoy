#include "common/router/on_demand_update.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/http/codes.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Router {

void OnDemandRouteUpdate::requestRouteConfigUpdate() {
  if (callbacks_->route() != nullptr) {
    filter_return_ = FilterReturn::ContinueDecoding;
  } else {
    auto configUpdateScheduled =
        callbacks_->requestRouteConfigUpdate([this]() -> void { onComplete(); });
    filter_return_ =
        configUpdateScheduled ? FilterReturn::StopDecoding : FilterReturn::ContinueDecoding;
  }
}

Http::FilterHeadersStatus OnDemandRouteUpdate::decodeHeaders(Http::HeaderMap&, bool) {
  requestRouteConfigUpdate();
  return filter_return_ == FilterReturn::StopDecoding ? Http::FilterHeadersStatus::StopIteration
                                                      : Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus OnDemandRouteUpdate::decodeData(Buffer::Instance&, bool) {
  return filter_return_ == FilterReturn::StopDecoding
             ? Http::FilterDataStatus::StopIterationAndWatermark
             : Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus OnDemandRouteUpdate::decodeTrailers(Http::HeaderMap&) {
  return filter_return_ == FilterReturn::StopDecoding ? Http::FilterTrailersStatus::StopIteration
                                                      : Http::FilterTrailersStatus::Continue;
}

void OnDemandRouteUpdate::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

void OnDemandRouteUpdate::onComplete() {
  filter_return_ = FilterReturn::ContinueDecoding;

  if (!callbacks_->decodingBuffer() && // Redirects with body not yet supported.
      callbacks_->recreateStream()) {
    // cluster_->stats().upstream_internal_redirect_succeeded_total_.inc();
    return;
  }

  // recreating stream failed, continue the filter-chain
  callbacks_->continueDecoding();
}

} // namespace Router
} // namespace Envoy
