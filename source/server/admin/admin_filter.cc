#include "source/server/admin/admin_filter.h"

#include "source/server/admin/utils.h"

namespace Envoy {
namespace Server {

AdminFilter::AdminFilter(Admin::GenHandlerCb admin_handler_fn)
    : admin_handler_fn_(admin_handler_fn) {}

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
  const absl::string_view path = request_headers_->getPathValue();
  ENVOY_STREAM_LOG(debug, "request complete: path: {}", *decoder_callbacks_, path);

  auto header_map = Http::ResponseHeaderMapImpl::create();
  RELEASE_ASSERT(request_headers_, "");
  handler_ = admin_handler_fn_(path, *this);
  Http::Code code = handler_->start(*header_map);
  Utility::populateFallbackResponseHeaders(code, *header_map);
  decoder_callbacks_->encodeHeaders(std::move(header_map), false,
                                    StreamInfo::ResponseCodeDetails::get().AdminFilterResponse);

  nextChunk();
}

void AdminFilter::onDecoderFilterAboveWriteBufferLowWatermark() {
  can_write_ = true;
  nextChunk();
}

void AdminFilter::nextChunk() {
  if (!can_write_ || handler_ == nullptr) {
    return;
  }

  Buffer::OwnedImpl response;
  bool more_data = handler_->nextChunk(response);
  bool end_stream = end_stream_on_complete_ && !more_data;
  if (response.length() > 0 || end_stream) {
    decoder_callbacks_->encodeData(response, end_stream);
  }
  if (more_data) {
    decoder_callbacks_->dispatcher().post([this](){ nextChunk(); });
  } else {
    handler_.reset();
  }
}

} // namespace Server
} // namespace Envoy
