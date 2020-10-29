#include "extensions/filters/http/response_map/response_map_filter.h"

#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"

#include "extensions/filters/http/well_known_names.h"

#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ResponseMapFilter {

ResponseMapFilterConfig::ResponseMapFilterConfig(
    const envoy::extensions::filters::http::response_map::v3::ResponseMap& proto_config,
    const std::string&,
    Server::Configuration::FactoryContext& context)
    : response_map_(ResponseMap::Factory::create(proto_config, context)) {}

ResponseMapFilter::ResponseMapFilter(ResponseMapFilterConfigSharedPtr config)
    : config_(config) {}

Http::FilterHeadersStatus ResponseMapFilter::decodeHeaders(Http::RequestHeaderMap& request_headers,
                                                           bool end_stream) {
  ENVOY_LOG(trace, "response map filter: decodeHeaders with end_stream = {}", end_stream);

  // Disable filter per route config if applies
  if (decoder_callbacks_->route() != nullptr) {
    const auto* per_route_config =
        Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfigPerRoute>(
            Extensions::HttpFilters::HttpFilterNames::get().ResponseMap,
            decoder_callbacks_->route());
    ENVOY_LOG(trace, "response map filter: found route. has per_route_config? {}",
        per_route_config != nullptr);
    if (per_route_config != nullptr && per_route_config->disabled()) {
      ENVOY_LOG(trace, "response map filter: disabling due to per_route_config");
      disabled_ = true;
      return Http::FilterHeadersStatus::Continue;
    }
  }

  request_headers_ = &request_headers;
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus ResponseMapFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                           bool end_stream) {
  ENVOY_LOG(trace,
      "response map filter: encodeHeaders with http status = {}, end_stream = {}, disabled = {}",
      headers.getStatusValue(), end_stream, disabled_);

  // If this filter is disabled, continue without doing anything.
  if (disabled_) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Save a reference to the response headers. If we end up rewriting the response,
  // we'll need to set the content-length (and possibly other) headers later.
  response_headers_ = &headers;

  // Check whether we should rewrite this response based on response headers.
  do_rewrite_ = config_->response_map()->match(
      request_headers_, headers, encoder_callbacks_->streamInfo());
  ENVOY_LOG(trace, "response map filter: do_rewrite_ {}", do_rewrite_);

  // If we decided not to rewrite the response, simply pass through to other
  // filters.
  if (!do_rewrite_) {
    return Http::FilterHeadersStatus::Continue;
  }

  // We know we're going to rewrite the response at this point. If the stream
  // is not yet finished, we need to prevent other filters from iterating until
  // we've had a chance to actually rewrite the body later in encodeData.
  if (!end_stream) {
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Now that the stream is complete, rewrite the response body.
  doRewrite();
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus ResponseMapFilter::encodeData(Buffer::Instance& data,
                                                     bool end_stream) {
  ENVOY_LOG(trace,
      "response map filter: encodeData with data length {}, end_stream = {}, disabled = {}",
      data.length(), end_stream, disabled_);

  // If this filter is disabled, continue without doing anything.
  if (disabled_) {
    return Http::FilterDataStatus::Continue;
  }

  // If we decided not to rewrite the response, simply pass through to other
  // filters.
  if (!do_rewrite_) {
      return Http::FilterDataStatus::Continue;
  }

  // We decided to rewrite the response, so drain any data received from the
  // upstream since we're rewriting it anyway. This both prevents unnecessary
  // buffering and also prevents generating errors if the response is too big
  // to be buffered completely.
  data.drain(data.length());

  // The stream is not yet complete, and we can't let other filters run
  // until we've rewritten the response body.
  if (!end_stream) {
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }

  // Now that the stream is complete, rewrite the response body.
  doRewrite();
  return Http::FilterDataStatus::Continue;
}

void ResponseMapFilter::doRewrite(void) {
  const Buffer::Instance* encoding_buffer = encoder_callbacks_->encodingBuffer();

  ENVOY_LOG(trace, "response map filter: doRewrite with {} encoding_buffer",
      encoding_buffer != nullptr ? "non-null" : "null");

  // If this route is disabled, we should never be doing a rewrite.
  // In fact, we never should have even checked if we should do
  // a rewrite.
  ASSERT(!disabled_);

  // We should either see no encoding buffer or an empty encoding buffer.
  //
  // We'll see no encoding buffer if the upstream response was never observed
  // (ie: due to error) or if the upstream response was headers-only. Otherwise,
  // if we did see a response, we should have drained any data we saw.
  ASSERT(encoding_buffer == nullptr || encoding_buffer->length() == 0);

  // Fill in new_body and new_content_type using the response map rewrite.
  // Pass in the request and response headers that we observed on the
  // decodeHeaders and encodeHeaders paths, respectively.
  std::string new_body;
  absl::string_view new_content_type;
  config_->response_map()->rewrite(
      request_headers_,
      *response_headers_,
      encoder_callbacks_->streamInfo(),
      new_body,
      new_content_type
      );

  // Encoding buffer may be null here even if we saw data in encodeData above. This
  // happens when sendLocalReply sends a response downstream. By adding encoded data
  // here, we do successfully override the sendLocalReply body, but it's not clear
  // how or why.
  if (encoding_buffer == nullptr) {
    // If we never saw a response body from the upstream, then we need to add encoded data
    // instead of modifying the encoding buffer, as we do below. That's because headers-only
    // upstream responses (or responses never received by the upstream) can only be transformed
    // into responses with a body using this method. See `include/envoy/http/filter.h`.
    //
    // We're not streaming back this rewritten body (ie: it's already formed in memory) so
    // we pass streaming_filter = false.
    Buffer::OwnedImpl body{new_body};
    const bool streaming_filter = false;
    encoder_callbacks_->addEncodedData(body, streaming_filter);
  } else {
    // We had a previous response from the upstream, so there's an existing encoding
    // buffer that we should modify. Realistically, the encoding buffer should be
    // empty here because we were draining data as it was received in encodeData,
    // but that's more-or-less an optimization so we drain it again here for
    // completeness.
    //
    // After we know the existing buffer has been drained of its existing data, we
    // modify it by adding the rewritten body so that it gets passed downstream.
    encoder_callbacks_->modifyEncodingBuffer([&new_body](Buffer::Instance& data) {
      Buffer::OwnedImpl body{new_body};
      data.drain(data.length());
      data.move(body);
    });
  }

  // Since we overwrote the response body, we need to set the content-length too.
  encoding_buffer = encoder_callbacks_->encodingBuffer();
  response_headers_->setContentLength(encoding_buffer->length());
  if (!new_content_type.empty()) {
    response_headers_->setContentType(new_content_type);
  }
}

} // namespace ResponseMapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
