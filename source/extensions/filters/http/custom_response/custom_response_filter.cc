#include "source/extensions/filters/http/custom_response/custom_response_filter.h"

#include "envoy/http/filter.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

Http::FilterHeadersStatus CustomResponseFilter::decodeHeaders(Http::RequestHeaderMap& header_map,
                                                              bool) {
  auto filter_state = encoder_callbacks_->streamInfo().filterState()->getDataReadOnly<Policy>(
      "envoy.filters.http.custom_response");
  if (!filter_state) {
    downstream_headers_ = &header_map;
    const auto* per_route_settings =
        Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfigPerRoute>(decoder_callbacks_);
    base_config_ = per_route_settings ? static_cast<const FilterConfigBase*>(per_route_settings)
                                      : static_cast<const FilterConfigBase*>(config_.get());
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus CustomResponseFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                              bool end_stream) {
  // If filter state for custom response exists, it means this response is a
  // custom response. Apply the custom response mutations to the response from
  // the remote source and return.
  auto filter_state = encoder_callbacks_->streamInfo().filterState()->getDataReadOnly<Policy>(
      "envoy.filters.http.custom_response");
  if (filter_state) {
    return filter_state->encodeHeaders(headers, end_stream, *this);
  }

  auto policy = base_config_->getPolicy(headers, encoder_callbacks_->streamInfo());

  // A valid custom response was not found. We should just pass through.
  if (!policy) {
    return Http::FilterHeadersStatus::Continue;
  }

  return policy->encodeHeaders(headers, end_stream, *this);
}

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
