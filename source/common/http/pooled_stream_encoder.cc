#include "pooled_stream_encoder.h"

#include "common/common/assert.h"
#include "common/http/headers.h"

namespace Http {

PooledStreamEncoder::PooledStreamEncoder(ConnectionPool::Instance& conn_pool,
                                         StreamDecoder& stream_decoder,
                                         StreamCallbacks& stream_callbacks, uint64_t connection_id,
                                         uint64_t stream_id,
                                         PooledStreamEncoderCallbacks& callbacks)
    : conn_pool_(conn_pool), stream_decoder_(stream_decoder), stream_callbacks_(stream_callbacks),
      connection_id_(connection_id), stream_id_(stream_id), callbacks_(callbacks) {}

void PooledStreamEncoder::commonEncodePrefix(bool end_stream) {
  ASSERT(!request_complete_);
  request_complete_ = end_stream;

  if (request_complete_) {
    request_complete_time_ = std::chrono::system_clock::now();
  }
}

void PooledStreamEncoder::encodeHeaders(const HeaderMap& headers, bool end_stream) {
  request_headers_ = &headers;
  commonEncodePrefix(end_stream);

  // Do a common header check. We make sure that all outgoing requests have all HTTP/2 headers.
  // These get stripped by HTTP/1 codec where applicable.
  ASSERT(headers.Scheme());
  ASSERT(headers.Method());
  ASSERT(headers.Host());
  ASSERT(headers.Path());

  // It's possible for a reset to happen inline within the newStream() call. In this case, we might
  // get deleted inline as well. Only write the returned handle out if it is not nullptr to deal
  // with this case.
  ConnectionPool::Cancellable* handle = conn_pool_.newStream(stream_decoder_, *this);
  if (handle) {
    conn_pool_stream_handle_ = handle;
  }
}

void PooledStreamEncoder::encodeData(Buffer::Instance& data, bool end_stream) {
  commonEncodePrefix(end_stream);
  if (!request_encoder_) {
    stream_log_trace("buffering {} bytes", *this, data.length());
    buffered_request_body_.move(data);
  } else {
    stream_log_trace("proxying {} bytes", *this, data.length());
    request_encoder_->encodeData(data, end_stream);
  }
}

void PooledStreamEncoder::encodeTrailers(const HeaderMap& trailers) {
  commonEncodePrefix(true);
  if (!request_encoder_) {
    stream_log_trace("buffering trailers", *this);
    request_trailers_ = &trailers;
  } else {
    stream_log_trace("proxying trailers", *this);
    request_encoder_->encodeTrailers(trailers);
  }
}

void PooledStreamEncoder::resetStream() {
  if (conn_pool_stream_handle_) {
    stream_log_debug("cancelling pool request", *this);
    ASSERT(!request_encoder_);
    conn_pool_stream_handle_->cancel();
    conn_pool_stream_handle_ = nullptr;
  }

  if (request_encoder_) {
    stream_log_debug("resetting pool request", *this);
    request_encoder_->getStream().removeCallbacks(*this);
    request_encoder_->getStream().resetStream(StreamResetReason::LocalReset);
  }
}

void PooledStreamEncoder::onPoolFailure(ConnectionPool::PoolFailureReason reason,
                                        Upstream::HostDescriptionPtr host) {
  StreamResetReason reset_reason = StreamResetReason::ConnectionFailure;
  switch (reason) {
  case ConnectionPool::PoolFailureReason::Overflow:
    reset_reason = StreamResetReason::Overflow;
    break;
  case ConnectionPool::PoolFailureReason::ConnectionFailure:
    reset_reason = StreamResetReason::ConnectionFailure;
    break;
  }

  // Mimic an upstream reset.
  callbacks_.onUpstreamHostSelected(host);
  stream_callbacks_.onResetStream(reset_reason);
}

void PooledStreamEncoder::onPoolReady(StreamEncoder& request_encoder,
                                      Upstream::HostDescriptionPtr host) {
  stream_log_debug("pool ready", *this);
  callbacks_.onUpstreamHostSelected(host);
  request_encoder.getStream().addCallbacks(*this);

  conn_pool_stream_handle_ = nullptr;
  request_encoder_ = &request_encoder;
  calling_encode_headers_ = true;
  request_encoder.encodeHeaders(*request_headers_, buffered_request_body_.length() == 0 &&
                                                       request_complete_ && !request_trailers_);
  calling_encode_headers_ = false;

  // It is possible to get reset in the middle of an encodeHeaders() call. This happens for example
  // in the http/2 codec if the frame cannot be encoded for some reason. This should never happen
  // but it's unclear if we have covered all cases so protect against it and test for it. One
  // specific example of a case where this happens is if we try to encode a total header size that
  // is too big in HTTP/2 (64K currently).
  if (deferred_reset_reason_.valid()) {
    stream_callbacks_.onResetStream(deferred_reset_reason_.value());
  } else {
    if (buffered_request_body_.length() > 0) {
      request_encoder.encodeData(buffered_request_body_, request_complete_ && !request_trailers_);
    }

    if (request_trailers_) {
      request_encoder.encodeTrailers(*request_trailers_);
    }
  }
}

void PooledStreamEncoder::onResetStream(StreamResetReason reason) {
  request_encoder_ = nullptr;
  if (!calling_encode_headers_) {
    stream_callbacks_.onResetStream(reason);
  } else {
    deferred_reset_reason_ = reason;
  }
}

} // Http
