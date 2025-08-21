#include "source/extensions/filters/http/custom_response/custom_response_filter.h"

#include "envoy/http/filter.h"
#include "envoy/router/router.h"
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
  // Check filter state for the existence of a custom response policy. The
  // expectation is that if a custom response policy recreates the stream, it
  // adds itself to the filter state. In that case do not look for
  // route-specific config, as this is not the original request from downstream.
  // Note that the original request header map is NOT carried over to the
  // redirected response. The redirected request header map does NOT participate
  // in the custom response framework.
  auto filter_state =
      encoder_callbacks_->streamInfo().filterState()->getDataReadOnly<CustomResponseFilterState>(
          CustomResponseFilterState::kFilterStateName);
  if (!filter_state) {
    downstream_headers_ = &header_map;
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus CustomResponseFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                              bool end_stream) {
  // If filter state for custom response exists, it means this response is a
  // custom response. Apply the custom response mutations to the response from
  // the remote source and return.
  auto filter_state =
      encoder_callbacks_->streamInfo().filterState()->getDataReadOnly<CustomResponseFilterState>(
          CustomResponseFilterState::kFilterStateName);
  if (filter_state) {
    return filter_state->policy->encodeHeaders(headers, end_stream, *this);
  }

  // Traverse up route typed per filter hierarchy till we find a matching
  // policy. Note that since the traversal is least to most specific, we can't
  // return early when a match is found.
  PolicySharedPtr policy;
  for (const FilterConfig& typed_config :
       Http::Utility::getAllPerFilterConfig<FilterConfig>(encoder_callbacks_)) {

    // Check if a match is found first to avoid overwriting policy with an
    // empty shared_ptr.
    auto maybe_policy = typed_config.getPolicy(headers, encoder_callbacks_->streamInfo());
    if (maybe_policy) {
      policy = maybe_policy;
    }
  }

  if (!policy) {
    policy = config_->getPolicy(headers, encoder_callbacks_->streamInfo());
  }

  // A valid custom response was not found. We should just pass through.
  if (!policy) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Apply the custom response policy.
  return policy->encodeHeaders(headers, end_stream, *this);
}

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
