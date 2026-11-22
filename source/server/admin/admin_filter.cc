#include "source/server/admin/admin_filter.h"

#include "source/common/runtime/runtime_features.h"
#include "source/server/admin/utils.h"

namespace Envoy {
namespace Server {

AdminFilter::AdminFilter(const Admin& admin)
    : admin_(admin), yield_between_chunks_(Runtime::runtimeFeatureEnabled(
                         "envoy.reloadable_features.admin_filter_yield_between_chunks")) {}

void AdminFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  PassThroughFilter::setDecoderFilterCallbacks(callbacks);
  // With a buffer limit on the admin connection, stop producing chunks while the client has not
  // drained the response buffered so far. Registering replays a high watermark that is already
  // raised, so the response may start paused.
  const auto connection = decoder_callbacks_->connection();
  if (!yield_between_chunks_ || !connection.has_value() || connection->bufferLimit() == 0) {
    return;
  }
  watermark_callbacks_registered_ = true;
  decoder_callbacks_->addDownstreamWatermarkCallbacks(*this);
}

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
  destroyed_ = true;
  next_chunk_cb_.reset();
  if (watermark_callbacks_registered_) {
    watermark_callbacks_registered_ = false;
    decoder_callbacks_->removeDownstreamWatermarkCallbacks(*this);
  }
  for (const auto& callback : on_destroy_callbacks_) {
    callback();
  }
  request_.reset();
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
  Admin::RequestPtr handler = admin_.makeRequest(*this);
  Http::Code code = handler->start(*header_map);
  Utility::populateFallbackResponseHeaders(code, *header_map);
  decoder_callbacks_->encodeHeaders(std::move(header_map), false,
                                    StreamInfo::ResponseCodeDetails::get().AdminFilterResponse);

  if (destroyed_) {
    return;
  }
  request_ = std::move(handler);
  if (!yield_between_chunks_) {
    while (encodeNextChunk()) {
    }
    return;
  }
  // The response may start paused by a replayed high watermark; the low watermark schedules the
  // first chunk then.
  if (paused()) {
    return;
  }
  // A chunk that pushes the downstream above its high watermark stops here; the low watermark
  // schedules the next one.
  if (const bool more_data = encodeNextChunk(); !more_data || paused()) {
    return;
  }
  scheduleNextChunk();
}

void AdminFilter::onAboveWriteBufferHighWatermark() {
  ENVOY_STREAM_LOG(debug, "admin response paused by downstream backpressure", *decoder_callbacks_);
  above_high_watermark_count_++;
}

void AdminFilter::onBelowWriteBufferLowWatermark() {
  ASSERT(above_high_watermark_count_ > 0);
  above_high_watermark_count_--;
  if (paused() || request_ == nullptr || destroyed_) {
    return;
  }
  ENVOY_STREAM_LOG(debug, "admin response resumed", *decoder_callbacks_);
  // This is called from the codec's write path, so continue on the next event loop iteration
  // rather than encoding inline.
  scheduleNextChunk();
}

bool AdminFilter::encodeNextChunk() {
  ASSERT(request_ != nullptr);
  Buffer::OwnedImpl response;
  const bool more_data = request_->nextChunk(response);
  const bool end_stream = end_stream_on_complete_ && !more_data;
  ENVOY_LOG_MISC(debug, "nextChunk: response.length={} more_data={} end_stream={}",
                 response.length(), more_data, end_stream);
  if (response.length() > 0 || end_stream) {
    // This may complete or reset the stream, which calls onDestroy().
    decoder_callbacks_->encodeData(response, end_stream);
  }
  if (destroyed_) {
    return false;
  }
  if (!more_data) {
    request_.reset();
    return false;
  }
  return true;
}

void AdminFilter::scheduleNextChunk() {
  if (next_chunk_cb_ == nullptr) {
    next_chunk_cb_ = decoder_callbacks_->dispatcher().createSchedulableCallback([this]() {
      // A low watermark during the last chunk may have scheduled this after the request is done.
      if (request_ == nullptr || paused()) {
        return;
      }
      if (const bool more_data = encodeNextChunk(); !more_data || paused()) {
        return;
      }
      scheduleNextChunk();
    });
  }
  // Yield to the event loop before the next chunk, so other main thread work like health checks
  // and xDS updates can run in between.
  next_chunk_cb_->scheduleCallbackNextIteration();
}

} // namespace Server
} // namespace Envoy
