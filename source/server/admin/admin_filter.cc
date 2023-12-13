#include "source/server/admin/admin_filter.h"

#include "source/server/admin/utils.h"

namespace Envoy {
namespace Server {

AdminFilter::AdminFilter(Admin::GenRequestFn admin_handler_fn)
    : admin_handler_fn_(admin_handler_fn) {}

Http::FilterHeadersStatus AdminFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                     bool end_stream) {
  request_headers_ = &headers;
  if (end_stream) {
    onComplete();
  }

  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus AdminFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  // Currently we generically buffer all admin request data in case a handler wants to use it.
  // If we ever support streaming admin requests we may need to revisit this. Note, we must use
  // addDecodedData() here since we might need to perform onComplete() processing if end_stream is
  // true.
  decoder_callbacks_->addDecodedData(data, false);

  if (end_stream) {
    onComplete();
  }

  return Http::FilterDataStatus::StopIterationNoBuffer;
}

Http::FilterTrailersStatus AdminFilter::decodeTrailers(Http::RequestTrailerMap&) {
  onComplete();
  return Http::FilterTrailersStatus::StopIteration;
}

void AdminFilter::onDestroy() {
  for (const auto& callback : on_destroy_callbacks_) {
    callback();
  }
}

void AdminFilter::addOnDestroyCallback(std::function<void()> cb) {
  on_destroy_callbacks_.push_back(std::move(cb));
}

Http::StreamDecoderFilterCallbacks& AdminFilter::getDecoderFilterCallbacks() const {
  ASSERT(decoder_callbacks_ != nullptr);
  return *decoder_callbacks_;
}

const Buffer::Instance* AdminFilter::getRequestBody() const {
  return decoder_callbacks_->decodingBuffer();
}

const Http::RequestHeaderMap& AdminFilter::getRequestHeaders() const {
  ASSERT(request_headers_ != nullptr);
  return *request_headers_;
}

Http::Utility::QueryParamsMulti AdminFilter::queryParams() const {
  absl::string_view path = request_headers_->getPathValue();
  Http::Utility::QueryParamsMulti query =
      Http::Utility::QueryParamsMulti::parseAndDecodeQueryString(path);
  if (!query.data().empty()) {
    return query;
  }

  // Check if the params are in the request's body.
  if (request_headers_->getContentTypeValue() ==
      Http::Headers::get().ContentTypeValues.FormUrlEncoded) {
    const Buffer::Instance* body = getRequestBody();
    if (body != nullptr) {
      query = Http::Utility::QueryParamsMulti::parseParameters(body->toString(), 0, true);
    }
  }

  return query;
}

void AdminFilter::onComplete() {
  absl::string_view path = request_headers_->getPathValue();
  ENVOY_STREAM_LOG(debug, "request complete: path: {}", *decoder_callbacks_, path);

  auto header_map = Http::ResponseHeaderMapImpl::create();
  RELEASE_ASSERT(request_headers_, "");
  Admin::RequestPtr handler = admin_handler_fn_(*this);
  Http::Code code = handler->start(*header_map);
  Utility::populateFallbackResponseHeaders(code, *header_map);
  decoder_callbacks_->encodeHeaders(std::move(header_map), false,
                                    StreamInfo::ResponseCodeDetails::get().AdminFilterResponse);

  bool more_data;
  do {
    Buffer::OwnedImpl response;
    more_data = handler->nextChunk(response);
    bool end_stream = end_stream_on_complete_ && !more_data;
    ENVOY_LOG_MISC(debug, "nextChunk: response.length={} more_data={} end_stream={}",
                   response.length(), more_data, end_stream);
    if (response.length() > 0 || end_stream) {
      decoder_callbacks_->encodeData(response, end_stream);
    }
  } while (more_data);
}

} // namespace Server
} // namespace Envoy
