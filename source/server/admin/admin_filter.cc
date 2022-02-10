#include "source/server/admin/admin_filter.h"

#include "source/server/admin/utils.h"

namespace Envoy {
namespace Server {

AdminFilter::AdminFilter(AdminHandlerFn admin_handler_fn)
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

void AdminFilter::onComplete() {
  const absl::string_view path = request_headers_->getPathValue();
  ENVOY_STREAM_LOG(debug, "request complete: path: {}", *decoder_callbacks_, path);

  auto header_map = Http::ResponseHeaderMapImpl::create();
  RELEASE_ASSERT(request_headers_, "");
  Admin::HandlerPtr handler = admin_handler_fn_(path, *this);
  Http::Code code = handler->start(/*path, */ *header_map);
  Utility::populateFallbackResponseHeaders(code, *header_map);
  decoder_callbacks_->encodeHeaders(std::move(header_map), false,
                                    //end_stream_on_complete_ && response.length() == 0,
                                    StreamInfo::ResponseCodeDetails::get().AdminFilterResponse);

  bool done = false;
  do {
    Buffer::OwnedImpl response;
    handler->nextChunk(response);
    done = response.length() == 0;
    decoder_callbacks_->encodeData(response, end_stream_on_complete_ && done);
  } while (!done);

  /*
  bool error = code != Http::Code::OK;
  if (response.length() > 0) {
    decoder_callbacks_->encodeData(response, end_stream_on_complete_ && error);
  }

  if (!error) {
    // TODO(jmarantz): In https://github.com/envoyproxy/envoy/pull/19898 we'll
    // take on using real flow-control, rather than simply aggregating the
    // chunks together.
    while (handler->nextChunk(response)) {
      decoder_callbacks_->encodeData(response, false);
      response.drain(response.length());
    }
    decoder_callbacks_->encodeData(response, end_stream_on_complete_);
  }
  */
}

} // namespace Server
} // namespace Envoy
