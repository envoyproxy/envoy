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
  downstream_headers_ = &header_map;
  const auto* per_route_settings =
      Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfigPerRoute>(decoder_callbacks_);
  base_config_ = per_route_settings ? static_cast<const FilterConfigBase*>(per_route_settings)
                                    : static_cast<const FilterConfigBase*>(config_.get());
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus CustomResponseFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                              bool end_stream) {
  (void)end_stream;
  // check if filterstate exists already.
  auto filter_state = encoder_callbacks_->streamInfo().filterState()->getDataReadOnly<Response>(
      "envoy.filters.http.custom_response");
  if (filter_state) {
    filter_state->evaluateHeaders(headers, encoder_callbacks_->streamInfo());
    // std::string body;
    // Http::Code code;
    // filter_state->rewriteBody(headers, encoder_callbacks_->streamInfo(), body, code);
  }
  auto custom_response = base_config_->getResponse(headers, encoder_callbacks_->streamInfo());

  // A valid custom response was not found. We should just pass through.
  if (!custom_response) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Handle remote body
  if (custom_response->isRemote()) {
    // Modify the request headers & recreate stream.
    ASSERT(downstream_headers_ != nullptr);
    auto& remote_data_source = custom_response->remoteDataSource();
    if (!remote_data_source.has_value()) {
      ENVOY_LOG(trace, "RemoteDataSource is empty");
      config_->stats().custom_response_redirect_invalid_uri_.inc();
      return Http::FilterHeadersStatus::Continue;
    }
    Http::Utility::Url absolute_url;
    if (!absolute_url.initialize(remote_data_source->uri(), false)) {
      ENVOY_LOG(trace, "Redirect for custom response failed: invalid location {}",
                remote_data_source->uri());
      config_->stats().custom_response_redirect_invalid_uri_.inc();
      return Http::FilterHeadersStatus::Continue;
    }

    // TODO: cache original host and path
    // TODO: filter state/metadata to track that we've done a redirect
    // Replace the original host, scheme and path.
    downstream_headers_->setScheme(absolute_url.scheme());
    downstream_headers_->setHost(absolute_url.hostAndPort());

    auto path_and_query = absolute_url.pathAndQueryParams();
    if (Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.http_reject_path_with_fragment")) {
      // Envoy treats internal redirect as a new request and will reject it if URI path
      // contains #fragment. However the Location header is allowed to have #fragment in URI path.
      // To prevent Envoy from rejecting internal redirect, strip the #fragment from Location URI if
      // it is present.
      auto fragment_pos = path_and_query.find('#');
      path_and_query = path_and_query.substr(0, fragment_pos);
    }
    downstream_headers_->setPath(path_and_query);

    if (decoder_callbacks_->downstreamCallbacks()) {
      decoder_callbacks_->downstreamCallbacks()->clearRouteCache();
    }
    const auto route = decoder_callbacks_->route();
    // Don't allow a redirect to a non existing route.
    if (!route) {
      config_->stats().custom_response_redirect_no_route_.inc();
      ENVOY_LOG(trace, "Redirect for custom response failed: no route found");
      return Http::FilterHeadersStatus::Continue;
    }
    downstream_headers_->setMethod(Http::Headers::get().MethodValues.Get);
    downstream_headers_->remove(Http::Headers::get().ContentLength);
    encoder_callbacks_->streamInfo().filterState()->setData(
        "envoy.filters.http.custom_response", custom_response,
        StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Request);
    // decoder_callbacks_->modifyDecodingBuffer(
    //[](Buffer::Instance& data) { data.drain(data.length()); });
    // decoder_callbacks_->recreateStream(&headers);
    decoder_callbacks_->recreateStream(nullptr);
    (void)factory_context_;

    return Http::FilterHeadersStatus::StopIteration;
  }

  // Handle local body
  std::string body;
  Http::Code code;
  custom_response->rewriteBody(headers, encoder_callbacks_->streamInfo(), body, code);

  const auto mutate_headers = [custom_response = custom_response,
                               this](Http::ResponseHeaderMap& headers) {
    custom_response->evaluateHeaders(headers, encoder_callbacks_->streamInfo());
  };
  encoder_callbacks_->sendLocalReply(code, body, mutate_headers, absl::nullopt, "");
  return Http::FilterHeadersStatus::StopIteration;
}

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
