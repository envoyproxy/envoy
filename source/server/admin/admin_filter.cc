#include "source/server/admin/admin_filter.h"

#include "source/server/admin/utils.h"

namespace Envoy {
namespace Server {

AdminFilter::AdminFilter(AdminServerCallbackFunction admin_server_callback_func)
    : admin_server_callback_func_(admin_server_callback_func) {}

Http::FilterHeadersStatus AdminFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                     bool end_stream) {
  //ENVOY_LOG_MISC(error, "decodeHeaders({})", end_stream);
  request_headers_ = &headers;
  if (end_stream) {
    onComplete();
  }

  return Http::FilterHeadersStatus::StopIteration;
}

#if 1
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

#else

Http::FilterDataStatus AdminFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  //ENVOY_LOG_MISC(error, "decodeData({})", end_stream);
  ASSERT(handler_ != nullptr);
  if (handler_->nextChunk(data)) {
    return Http::FilterDataStatus::Continue;
  }
  return Http::FilterDataStatus::StopIterationNoBuffer;
}
#endif

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


void AdminFilter::onComplete() {
  if (handler_ != nullptr) {
    nextChunk();
    return;
  }

  const absl::string_view path = request_headers_->getPathValue();
  ENVOY_STREAM_LOG(debug, "request complete: path: {}", *decoder_callbacks_, path);
  auto header_map = Http::ResponseHeaderMapImpl::create();
  Buffer::OwnedImpl response;
  handler_ = admin_server_callback_func_(path, *header_map, response, *this);
  Http::Code code = handler_->start(path, *header_map, response);
  Utility::populateFallbackResponseHeaders(code, *header_map);
  decoder_callbacks_->encodeHeaders(std::move(header_map),
                                    end_stream_on_complete_ && response.length() == 0,
                                    StreamInfo::ResponseCodeDetails::get().AdminFilterResponse);
  if (response.length() > 0) {
    decoder_callbacks_->encodeData(response, false);
    decoder_callbacks_->dispatcher().post([this](){ nextChunk(); });
  }

  /*
  if (code == Http::Code::OK) {
    return Http::FilterHeadersStatus::Continue;
  }
  */

  /*
  const absl::string_view path = request_headers_->getPathValue();
  ENVOY_STREAM_LOG(debug, "request complete: path: {}", *decoder_callbacks_, path);

  Buffer::OwnedImpl response;
  auto header_map = Http::ResponseHeaderMapImpl::create();
  RELEASE_ASSERT(request_headers_, "");
  Http::Code code = admin_server_callback_func_(path, *header_map, response, *this);
  Utility::populateFallbackResponseHeaders(code, *header_map);
  decoder_callbacks_->encodeHeaders(std::move(header_map),
                                    end_stream_on_complete_ && response.length() == 0,
                                    StreamInfo::ResponseCodeDetails::get().AdminFilterResponse);

  if (response.length() > 0) {
    decoder_callbacks_->encodeData(response, end_stream_on_complete_);
  }
  */
}

void AdminFilter::nextChunk() {
  //ENVOY_LOG_MISC(error, "emitChunk()");
  Buffer::OwnedImpl response;
  bool more_data = handler_->nextChunk(response);
  if (response.length() > 0 || !more_data) {
    decoder_callbacks_->encodeData(response, end_stream_on_complete_ && !more_data);
  }
  if (more_data) {
    decoder_callbacks_->dispatcher().post([this]() { nextChunk(); });
  }
}

} // namespace Server
} // namespace Envoy
