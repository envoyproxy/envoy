#include "common/http/http2/codec_impl.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/http/codes.h"
#include "common/http/exception.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Http {
namespace Http2 {

bool Utility::reconstituteCrumbledCookies(const HeaderString& key, const HeaderString& value,
                                          HeaderString& cookies) {
  if (key != Headers::get().Cookie.get().c_str()) {
    return false;
  }

  if (!cookies.empty()) {
    cookies.append("; ", 2);
  }

  cookies.append(value.c_str(), value.size());
  return true;
}

ConnectionImpl::Http2Callbacks ConnectionImpl::http2_callbacks_;
ConnectionImpl::Http2Options ConnectionImpl::http2_options_;

/**
 * Helper to remove const during a cast. nghttp2 takes non-const pointers for headers even though
 * it copies them.
 */
template <typename T> static T* remove_const(const void* object) {
  return const_cast<T*>(reinterpret_cast<const T*>(object));
}

ConnectionImpl::StreamImpl::StreamImpl(ConnectionImpl& parent, uint32_t buffer_limit)
    : parent_(parent), headers_(new HeaderMapImpl()), local_end_stream_sent_(false),
      remote_end_stream_(false), data_deferred_(false),
      waiting_for_non_informational_headers_(false),
      pending_receive_buffer_high_watermark_called_(false),
      pending_send_buffer_high_watermark_called_(false) {
  if (buffer_limit > 0) {
    setWriteBufferWatermarks(buffer_limit / 2, buffer_limit);
  }
}

static void insertHeader(std::vector<nghttp2_nv>& headers, const HeaderEntry& header) {
  uint8_t flags = 0;
  if (header.key().type() == HeaderString::Type::Reference) {
    flags |= NGHTTP2_NV_FLAG_NO_COPY_NAME;
  }
  if (header.value().type() == HeaderString::Type::Reference) {
    flags |= NGHTTP2_NV_FLAG_NO_COPY_VALUE;
  }
  headers.push_back({remove_const<uint8_t>(header.key().c_str()),
                     remove_const<uint8_t>(header.value().c_str()), header.key().size(),
                     header.value().size(), flags});
}

void ConnectionImpl::StreamImpl::buildHeaders(std::vector<nghttp2_nv>& final_headers,
                                              const HeaderMap& headers) {
  // nghttp2 requires that all ':' headers come before all other headers. To avoid making higher
  // layers understand that we do two passes here to build the final header list to encode.
  final_headers.reserve(headers.size());
  headers.iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        std::vector<nghttp2_nv>* final_headers = static_cast<std::vector<nghttp2_nv>*>(context);
        if (header.key().c_str()[0] == ':') {
          insertHeader(*final_headers, header);
        }
        return HeaderMap::Iterate::Continue;
      },
      &final_headers);

  headers.iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        std::vector<nghttp2_nv>* final_headers = static_cast<std::vector<nghttp2_nv>*>(context);
        if (header.key().c_str()[0] != ':') {
          insertHeader(*final_headers, header);
        }
        return HeaderMap::Iterate::Continue;
      },
      &final_headers);
}

void ConnectionImpl::StreamImpl::encode100ContinueHeaders(const HeaderMap& headers) {
  ASSERT(headers.Status()->value() == "100");
  encodeHeaders(headers, false);
}

void ConnectionImpl::StreamImpl::encodeHeaders(const HeaderMap& headers, bool end_stream) {
  std::vector<nghttp2_nv> final_headers;
  buildHeaders(final_headers, headers);

  nghttp2_data_provider provider;
  if (!end_stream) {
    provider.source.ptr = this;
    provider.read_callback = [](nghttp2_session*, int32_t, uint8_t*, size_t length,
                                uint32_t* data_flags, nghttp2_data_source* source,
                                void*) -> ssize_t {
      return static_cast<StreamImpl*>(source->ptr)->onDataSourceRead(length, data_flags);
    };
  }

  local_end_stream_ = end_stream;
  submitHeaders(final_headers, end_stream ? nullptr : &provider);
  parent_.sendPendingFrames();
}

void ConnectionImpl::StreamImpl::encodeTrailers(const HeaderMap& trailers) {
  ASSERT(!local_end_stream_);
  local_end_stream_ = true;
  if (pending_send_data_.length() > 0) {
    // In this case we want trailers to come after we release all pending body data that is
    // waiting on window updates. We need to save the trailers so that we can emit them later.
    ASSERT(!pending_trailers_);
    pending_trailers_.reset(new HeaderMapImpl(trailers));
  } else {
    submitTrailers(trailers);
    parent_.sendPendingFrames();
  }
}
void ConnectionImpl::StreamImpl::readDisable(bool disable) {
  ENVOY_CONN_LOG(debug, "Stream {} {}, unconsumed_bytes {} read_disable_count {}",
                 parent_.connection_, stream_id_, (disable ? "disabled" : "enabled"),
                 unconsumed_bytes_, read_disable_count_);
  if (disable) {
    ++read_disable_count_;
  } else {
    ASSERT(read_disable_count_ > 0);
    --read_disable_count_;
    if (!buffers_overrun()) {
      nghttp2_session_consume(parent_.session_, stream_id_, unconsumed_bytes_);
      unconsumed_bytes_ = 0;
      parent_.sendPendingFrames();
    }
  }
}

void ConnectionImpl::StreamImpl::pendingRecvBufferHighWatermark() {
  ENVOY_CONN_LOG(debug, "recv buffer over limit ", parent_.connection_);
  ASSERT(!pending_receive_buffer_high_watermark_called_);
  pending_receive_buffer_high_watermark_called_ = true;
  readDisable(true);
}

void ConnectionImpl::StreamImpl::pendingRecvBufferLowWatermark() {
  ENVOY_CONN_LOG(debug, "recv buffer under limit ", parent_.connection_);
  ASSERT(pending_receive_buffer_high_watermark_called_);
  pending_receive_buffer_high_watermark_called_ = false;
  readDisable(false);
}

void ConnectionImpl::StreamImpl::pendingSendBufferHighWatermark() {
  ENVOY_CONN_LOG(debug, "send buffer over limit ", parent_.connection_);
  ASSERT(!pending_send_buffer_high_watermark_called_);
  pending_send_buffer_high_watermark_called_ = true;
  runHighWatermarkCallbacks();
}

void ConnectionImpl::StreamImpl::pendingSendBufferLowWatermark() {
  ENVOY_CONN_LOG(debug, "send buffer under limit ", parent_.connection_);
  ASSERT(pending_send_buffer_high_watermark_called_);
  pending_send_buffer_high_watermark_called_ = false;
  runLowWatermarkCallbacks();
}

void ConnectionImpl::StreamImpl::saveHeader(HeaderString&& name, HeaderString&& value) {
  if (!Utility::reconstituteCrumbledCookies(name, value, cookies_)) {
    headers_->addViaMove(std::move(name), std::move(value));
  }
}

void ConnectionImpl::StreamImpl::submitTrailers(const HeaderMap& trailers) {
  std::vector<nghttp2_nv> final_headers;
  buildHeaders(final_headers, trailers);
  int rc =
      nghttp2_submit_trailer(parent_.session_, stream_id_, &final_headers[0], final_headers.size());
  ASSERT(rc == 0);
  UNREFERENCED_PARAMETER(rc);
}

ssize_t ConnectionImpl::StreamImpl::onDataSourceRead(uint64_t length, uint32_t* data_flags) {
  if (pending_send_data_.length() == 0 && !local_end_stream_) {
    ASSERT(!data_deferred_);
    data_deferred_ = true;
    return NGHTTP2_ERR_DEFERRED;
  } else {
    *data_flags |= NGHTTP2_DATA_FLAG_NO_COPY;
    if (local_end_stream_ && pending_send_data_.length() <= length) {
      *data_flags |= NGHTTP2_DATA_FLAG_EOF;
      if (pending_trailers_) {
        // We need to tell the library to not set end stream so that we can emit the trailers.
        *data_flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;
        submitTrailers(*pending_trailers_);
        pending_trailers_.reset();
      }
    }

    return std::min(length, pending_send_data_.length());
  }
}

int ConnectionImpl::StreamImpl::onDataSourceSend(const uint8_t* framehd, size_t length) {
  // In this callback we are writing out a raw DATA frame without copying. nghttp2 assumes that we
  // "just know" that the frame header is 9 bytes.
  // https://nghttp2.org/documentation/types.html#c.nghttp2_send_data_callback
  static const uint64_t FRAME_HEADER_SIZE = 9;

  Buffer::OwnedImpl output(framehd, FRAME_HEADER_SIZE);
  output.move(pending_send_data_, length);
  parent_.connection_.write(output, false);
  return 0;
}

void ConnectionImpl::ClientStreamImpl::submitHeaders(const std::vector<nghttp2_nv>& final_headers,
                                                     nghttp2_data_provider* provider) {
  ASSERT(stream_id_ == -1);
  stream_id_ = nghttp2_submit_request(parent_.session_, nullptr, &final_headers.data()[0],
                                      final_headers.size(), provider, base());
  ASSERT(stream_id_ > 0);
}

void ConnectionImpl::ServerStreamImpl::submitHeaders(const std::vector<nghttp2_nv>& final_headers,
                                                     nghttp2_data_provider* provider) {
  ASSERT(stream_id_ != -1);
  int rc = nghttp2_submit_response(parent_.session_, stream_id_, &final_headers.data()[0],
                                   final_headers.size(), provider);
  ASSERT(rc == 0);
  UNREFERENCED_PARAMETER(rc);
}

void ConnectionImpl::StreamImpl::encodeData(Buffer::Instance& data, bool end_stream) {
  ASSERT(!local_end_stream_);
  local_end_stream_ = end_stream;
  pending_send_data_.move(data);
  if (data_deferred_) {
    int rc = nghttp2_session_resume_data(parent_.session_, stream_id_);
    ASSERT(rc == 0);
    UNREFERENCED_PARAMETER(rc);

    data_deferred_ = false;
  }

  parent_.sendPendingFrames();
}

void ConnectionImpl::StreamImpl::resetStream(StreamResetReason reason) {
  // Higher layers expect calling resetStream() to immediately raise reset callbacks.
  runResetCallbacks(reason);

  // If we submit a reset, nghttp2 will cancel outbound frames that have not yet been sent.
  // We want these frames to go out so we defer the reset until we send all of the frames that
  // end the local stream.
  if (local_end_stream_ && !local_end_stream_sent_) {
    parent_.pending_deferred_reset_ = true;
    deferred_reset_ = reason;
    ENVOY_CONN_LOG(trace, "deferred reset stream", parent_.connection_);
  } else {
    resetStreamWorker(reason);
  }

  // We must still call sendPendingFrames() in both the deferred and not deferred path. This forces
  // the cleanup logic to run which will reset the stream in all cases if all data frames could not
  // be sent.
  parent_.sendPendingFrames();
}

void ConnectionImpl::StreamImpl::resetStreamWorker(StreamResetReason reason) {
  int rc = nghttp2_submit_rst_stream(parent_.session_, NGHTTP2_FLAG_NONE, stream_id_,
                                     reason == StreamResetReason::LocalRefusedStreamReset
                                         ? NGHTTP2_REFUSED_STREAM
                                         : NGHTTP2_NO_ERROR);
  ASSERT(rc == 0);
  UNREFERENCED_PARAMETER(rc);
}

ConnectionImpl::~ConnectionImpl() { nghttp2_session_del(session_); }

void ConnectionImpl::dispatch(Buffer::Instance& data) {
  ENVOY_CONN_LOG(trace, "dispatching {} bytes", connection_, data.length());
  uint64_t num_slices = data.getRawSlices(nullptr, 0);
  Buffer::RawSlice slices[num_slices];
  data.getRawSlices(slices, num_slices);
  for (Buffer::RawSlice& slice : slices) {
    dispatching_ = true;
    ssize_t rc =
        nghttp2_session_mem_recv(session_, static_cast<const uint8_t*>(slice.mem_), slice.len_);
    if (rc != static_cast<ssize_t>(slice.len_)) {
      throw CodecProtocolException(fmt::format("{}", nghttp2_strerror(rc)));
    }

    dispatching_ = false;
  }

  ENVOY_CONN_LOG(trace, "dispatched {} bytes", connection_, data.length());
  data.drain(data.length());

  // Decoding incoming frames can generate outbound frames so flush pending.
  sendPendingFrames();
}

ConnectionImpl::StreamImpl* ConnectionImpl::getStream(int32_t stream_id) {
  return static_cast<StreamImpl*>(nghttp2_session_get_stream_user_data(session_, stream_id));
}

int ConnectionImpl::onData(int32_t stream_id, const uint8_t* data, size_t len) {
  StreamImpl* stream = getStream(stream_id);
  // If this results in buffering too much data, the watermark buffer will call
  // pendingRecvBufferHighWatermark, resulting in ++read_disable_count_
  stream->pending_recv_data_.add(data, len);
  // Update the window to the peer unless some consumer of this stream's data has hit a flow control
  // limit and disabled reads on this stream
  if (!stream->buffers_overrun()) {
    nghttp2_session_consume(session_, stream_id, len);
  } else {
    stream->unconsumed_bytes_ += len;
  }
  return 0;
}

void ConnectionImpl::goAway() {
  int rc = nghttp2_submit_goaway(session_, NGHTTP2_FLAG_NONE,
                                 nghttp2_session_get_last_proc_stream_id(session_),
                                 NGHTTP2_NO_ERROR, nullptr, 0);
  ASSERT(rc == 0);
  UNREFERENCED_PARAMETER(rc);

  sendPendingFrames();
}

void ConnectionImpl::shutdownNotice() {
  int rc = nghttp2_submit_shutdown_notice(session_);
  ASSERT(rc == 0);
  UNREFERENCED_PARAMETER(rc);

  sendPendingFrames();
}

int ConnectionImpl::onFrameReceived(const nghttp2_frame* frame) {
  ENVOY_CONN_LOG(trace, "recv frame type={}", connection_, static_cast<uint64_t>(frame->hd.type));

  // Only raise GOAWAY once, since we don't currently expose stream information. Shutdown
  // notifications are the same as a normal GOAWAY.
  if (frame->hd.type == NGHTTP2_GOAWAY && !raised_goaway_) {
    ASSERT(frame->hd.stream_id == 0);
    raised_goaway_ = true;
    callbacks().onGoAway();
    return 0;
  }

  StreamImpl* stream = getStream(frame->hd.stream_id);
  if (!stream) {
    return 0;
  }

  switch (frame->hd.type) {
  case NGHTTP2_HEADERS: {
    stream->remote_end_stream_ = frame->hd.flags & NGHTTP2_FLAG_END_STREAM;
    if (!stream->cookies_.empty()) {
      HeaderString key(Headers::get().Cookie);
      stream->headers_->addViaMove(std::move(key), std::move(stream->cookies_));
    }

    switch (frame->headers.cat) {
    case NGHTTP2_HCAT_RESPONSE: {
      if (CodeUtility::is1xx(Http::Utility::getResponseStatus(*stream->headers_))) {
        stream->waiting_for_non_informational_headers_ = true;
      }

      if (stream->headers_->Status()->value() == "100") {
        ASSERT(!stream->remote_end_stream_);
        stream->decoder_->decode100ContinueHeaders(std::move(stream->headers_));
      } else {
        stream->decoder_->decodeHeaders(std::move(stream->headers_), stream->remote_end_stream_);
      }
      break;
    }

    case NGHTTP2_HCAT_REQUEST: {
      stream->decoder_->decodeHeaders(std::move(stream->headers_), stream->remote_end_stream_);
      break;
    }

    case NGHTTP2_HCAT_HEADERS: {
      // It's possible that we are waiting to send a deferred reset, so only raise headers/trailers
      // if local is not complete.
      if (!stream->deferred_reset_) {
        if (!stream->waiting_for_non_informational_headers_) {
          if (!stream->remote_end_stream_) {
            // This indicates we have received more headers frames than Envoy
            // supports. Even if this is valid HTTP (something like 103 early hints) fail here
            // rather than trying to push unexpected headers through the Envoy pipeline as that
            // will likely result in Envoy crashing.
            // It would be cleaner to reset the stream rather than reset the/ entire connection but
            // it's also slightly more dangerous so currently we err on the side of safety.
            stats_.too_many_header_frames_.inc();
            throw CodecProtocolException("Unexpected 'trailers' with no end stream.");
          } else {
            stream->decoder_->decodeTrailers(std::move(stream->headers_));
          }
        } else {
          ASSERT(!nghttp2_session_check_server_session(session_));
          stream->waiting_for_non_informational_headers_ = false;

          // This can only happen in the client case in a response, when we received a 1xx to
          // start out with. In this case, raise as headers. nghttp2 message checking guarantees
          // proper flow here.
          ASSERT(!stream->headers_->Status() || stream->headers_->Status()->value() != "100");
          stream->decoder_->decodeHeaders(std::move(stream->headers_), stream->remote_end_stream_);
        }
      }

      break;
    }

    default:
      // We do not currently support push.
      NOT_IMPLEMENTED;
    }

    stream->headers_.reset();
    break;
  }
  case NGHTTP2_DATA: {
    stream->remote_end_stream_ = frame->hd.flags & NGHTTP2_FLAG_END_STREAM;

    // It's possible that we are waiting to send a deferred reset, so only raise data if local
    // is not complete.
    if (!stream->deferred_reset_) {
      stream->decoder_->decodeData(stream->pending_recv_data_, stream->remote_end_stream_);
    }

    stream->pending_recv_data_.drain(stream->pending_recv_data_.length());
    break;
  }
  case NGHTTP2_RST_STREAM: {
    ENVOY_CONN_LOG(trace, "remote reset: {}", connection_, frame->rst_stream.error_code);
    stats_.rx_reset_.inc();
    break;
  }
  }

  return 0;
}

int ConnectionImpl::onFrameSend(const nghttp2_frame* frame) {
  // The nghttp2 library does not cleanly give us a way to determine whether we received invalid
  // data from our peer. Sometimes it raises the invalid frame callback, and sometimes it does not.
  // In all cases however it will attempt to send a GOAWAY frame with an error status. If we see
  // an outgoing frame of this type, we will return an error code so that we can abort execution.
  ENVOY_CONN_LOG(trace, "sent frame type={}", connection_, static_cast<uint64_t>(frame->hd.type));
  switch (frame->hd.type) {
  case NGHTTP2_GOAWAY: {
    if (frame->goaway.error_code != NGHTTP2_NO_ERROR) {
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    break;
  }

  case NGHTTP2_RST_STREAM: {
    ENVOY_CONN_LOG(debug, "sent reset code={}", connection_, frame->rst_stream.error_code);
    stats_.tx_reset_.inc();
    break;
  }

  case NGHTTP2_HEADERS:
  case NGHTTP2_DATA: {
    StreamImpl* stream = getStream(frame->hd.stream_id);
    stream->local_end_stream_sent_ = frame->hd.flags & NGHTTP2_FLAG_END_STREAM;
    break;
  }
  }

  return 0;
}

int ConnectionImpl::onInvalidFrame(int error_code) {
  ENVOY_CONN_LOG(debug, "invalid frame: {}", connection_, nghttp2_strerror(error_code));

  // The stream is about to be closed due to an invalid header. Don't kill the
  // entire connection if one stream has bad headers.
  if (error_code == NGHTTP2_ERR_HTTP_HEADER) {
    return 0;
  }

  // Cause dispatch to return with an error code.
  return NGHTTP2_ERR_CALLBACK_FAILURE;
}

ssize_t ConnectionImpl::onSend(const uint8_t* data, size_t length) {
  ENVOY_CONN_LOG(trace, "send data: bytes={}", connection_, length);
  Buffer::OwnedImpl buffer(data, length);
  connection_.write(buffer, false);
  return length;
}

int ConnectionImpl::onStreamClose(int32_t stream_id, uint32_t error_code) {
  UNREFERENCED_PARAMETER(error_code);

  StreamImpl* stream = getStream(stream_id);
  if (stream) {
    ENVOY_CONN_LOG(debug, "stream closed: {}", connection_, error_code);
    if (!stream->remote_end_stream_ || !stream->local_end_stream_) {
      stream->runResetCallbacks(error_code == NGHTTP2_REFUSED_STREAM
                                    ? StreamResetReason::RemoteRefusedStreamReset
                                    : StreamResetReason::RemoteReset);
    }

    connection_.dispatcher().deferredDelete(stream->removeFromList(active_streams_));
    // Any unconsumed data must be consumed before the stream is deleted.
    // nghttp2 does not appear to track this internally, and any stream deleted
    // with outstanding window will contribute to a slow connection-window leak.
    nghttp2_session_consume(session_, stream_id, stream->unconsumed_bytes_);
    stream->unconsumed_bytes_ = 0;
    nghttp2_session_set_stream_user_data(session_, stream->stream_id_, nullptr);
  }

  return 0;
}

int ConnectionImpl::saveHeader(const nghttp2_frame* frame, HeaderString&& name,
                               HeaderString&& value) {
  StreamImpl* stream = getStream(frame->hd.stream_id);
  if (!stream) {
    // We have seen 1 or 2 crashes where we get a headers callback but there is no associated
    // stream data. I honestly am not sure how this can happen. However, from reading the nghttp2
    // code it looks possible that inflate_header_block() can safely inflate headers for an already
    // closed stream, but will still call the headers callback. Since that seems possible, we should
    // ignore this case here.
    // TODO(mattklein123): Figure out a test case that can hit this.
    stats_.headers_cb_no_stream_.inc();
    return 0;
  }

  stream->saveHeader(std::move(name), std::move(value));
  if (stream->headers_->byteSize() > StreamImpl::MAX_HEADER_SIZE) {
    // This will cause the library to reset/close the stream.
    stats_.header_overflow_.inc();
    return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
  } else {
    return 0;
  }
}

void ConnectionImpl::sendPendingFrames() {
  if (dispatching_ || connection_.state() == Network::Connection::State::Closed) {
    return;
  }

  int rc = nghttp2_session_send(session_);
  if (rc != 0) {
    ASSERT(rc == NGHTTP2_ERR_CALLBACK_FAILURE);
    throw CodecProtocolException(fmt::format("{}", nghttp2_strerror(rc)));
  }

  // See ConnectionImpl::StreamImpl::resetStream() for why we do this. This is an uncommon event,
  // so iterating through every stream to find the ones that have a deferred reset is not a big
  // deal. Furthermore, queueing a reset frame does not actually invoke the close stream callback.
  // This is only done when the reset frame is sent. Thus, it's safe to work directly with the
  // stream map.
  // NOTE: The way we handle deferred reset is essentially best effort. If we intend to do a
  //       deferred reset, we try to finish the stream, including writing any pending data frames.
  //       If we cannot do this (potentially due to not enough window), we just reset the stream.
  //       In general this behavior occurs only when we are trying to send immediate error messages
  //       to short circuit requests. In the best effort case, we complete the stream before
  //       resetting. In other cases, we just do the reset now which will blow away pending data
  //       frames and release any memory associated with the stream.
  if (pending_deferred_reset_) {
    pending_deferred_reset_ = false;
    for (auto& stream : active_streams_) {
      if (stream->deferred_reset_) {
        stream->resetStreamWorker(stream->deferred_reset_.value());
      }
    }
    sendPendingFrames();
  }
}

void ConnectionImpl::sendSettings(const Http2Settings& http2_settings, bool disable_push) {
  ASSERT(http2_settings.hpack_table_size_ <= Http2Settings::MAX_HPACK_TABLE_SIZE);
  ASSERT(Http2Settings::MIN_MAX_CONCURRENT_STREAMS <= http2_settings.max_concurrent_streams_ &&
         http2_settings.max_concurrent_streams_ <= Http2Settings::MAX_MAX_CONCURRENT_STREAMS);
  ASSERT(
      Http2Settings::MIN_INITIAL_STREAM_WINDOW_SIZE <= http2_settings.initial_stream_window_size_ &&
      http2_settings.initial_stream_window_size_ <= Http2Settings::MAX_INITIAL_STREAM_WINDOW_SIZE);
  ASSERT(Http2Settings::MIN_INITIAL_CONNECTION_WINDOW_SIZE <=
             http2_settings.initial_connection_window_size_ &&
         http2_settings.initial_connection_window_size_ <=
             Http2Settings::MAX_INITIAL_CONNECTION_WINDOW_SIZE);

  std::vector<nghttp2_settings_entry> iv;

  if (http2_settings.hpack_table_size_ != NGHTTP2_DEFAULT_HEADER_TABLE_SIZE) {
    iv.push_back({NGHTTP2_SETTINGS_HEADER_TABLE_SIZE, http2_settings.hpack_table_size_});
    ENVOY_CONN_LOG(debug, "setting HPACK table size to {}", connection_,
                   http2_settings.hpack_table_size_);
  }

  if (http2_settings.max_concurrent_streams_ != NGHTTP2_INITIAL_MAX_CONCURRENT_STREAMS) {
    iv.push_back({NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, http2_settings.max_concurrent_streams_});
    ENVOY_CONN_LOG(debug, "setting max concurrent streams to {}", connection_,
                   http2_settings.max_concurrent_streams_);
  }

  if (http2_settings.initial_stream_window_size_ != NGHTTP2_INITIAL_WINDOW_SIZE) {
    iv.push_back(
        {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, http2_settings.initial_stream_window_size_});
    ENVOY_CONN_LOG(debug, "setting stream-level initial window size to {}", connection_,
                   http2_settings.initial_stream_window_size_);
  }

  if (disable_push) {
    // Universally disable receiving push promise frames as we don't currently support them. nghttp2
    // will fail the connection if the other side still sends them.
    // TODO(mattklein123): Remove this when we correctly proxy push promise.
    iv.push_back({NGHTTP2_SETTINGS_ENABLE_PUSH, 0});
  }

  if (!iv.empty()) {
    int rc = nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE, &iv[0], iv.size());
    ASSERT(rc == 0);
    UNREFERENCED_PARAMETER(rc);
  } else {
    // nghttp2_submit_settings need to be called at least once
    int rc = nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE, 0, 0);
    ASSERT(rc == 0);
    UNREFERENCED_PARAMETER(rc);
  }

  // Increase connection window size up to our default size.
  if (http2_settings.initial_connection_window_size_ != NGHTTP2_INITIAL_CONNECTION_WINDOW_SIZE) {
    ENVOY_CONN_LOG(debug, "updating connection-level initial window size to {}", connection_,
                   http2_settings.initial_connection_window_size_);
    int rc = nghttp2_submit_window_update(session_, NGHTTP2_FLAG_NONE, 0,
                                          http2_settings.initial_connection_window_size_ -
                                              NGHTTP2_INITIAL_CONNECTION_WINDOW_SIZE);
    ASSERT(rc == 0);
    UNREFERENCED_PARAMETER(rc);
  }
}

ConnectionImpl::Http2Callbacks::Http2Callbacks() {
  nghttp2_session_callbacks_new(&callbacks_);
  nghttp2_session_callbacks_set_send_callback(
      callbacks_,
      [](nghttp2_session*, const uint8_t* data, size_t length, int, void* user_data) -> ssize_t {
        return static_cast<ConnectionImpl*>(user_data)->onSend(data, length);
      });

  nghttp2_session_callbacks_set_send_data_callback(
      callbacks_,
      [](nghttp2_session*, nghttp2_frame* frame, const uint8_t* framehd, size_t length,
         nghttp2_data_source* source, void*) -> int {
        ASSERT(frame->data.padlen == 0);
        UNREFERENCED_PARAMETER(frame);
        return static_cast<StreamImpl*>(source->ptr)->onDataSourceSend(framehd, length);
      });

  nghttp2_session_callbacks_set_on_begin_headers_callback(
      callbacks_, [](nghttp2_session*, const nghttp2_frame* frame, void* user_data) -> int {
        return static_cast<ConnectionImpl*>(user_data)->onBeginHeaders(frame);
      });

  nghttp2_session_callbacks_set_on_header_callback(
      callbacks_,
      [](nghttp2_session*, const nghttp2_frame* frame, const uint8_t* raw_name, size_t name_length,
         const uint8_t* raw_value, size_t value_length, uint8_t, void* user_data) -> int {

        // TODO PERF: Can reference count here to avoid copies.
        HeaderString name;
        name.setCopy(reinterpret_cast<const char*>(raw_name), name_length);
        HeaderString value;
        value.setCopy(reinterpret_cast<const char*>(raw_value), value_length);
        return static_cast<ConnectionImpl*>(user_data)->onHeader(frame, std::move(name),
                                                                 std::move(value));
      });

  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
      callbacks_,
      [](nghttp2_session*, uint8_t, int32_t stream_id, const uint8_t* data, size_t len,
         void* user_data) -> int {
        return static_cast<ConnectionImpl*>(user_data)->onData(stream_id, data, len);
      });

  nghttp2_session_callbacks_set_on_frame_recv_callback(
      callbacks_, [](nghttp2_session*, const nghttp2_frame* frame, void* user_data) -> int {
        return static_cast<ConnectionImpl*>(user_data)->onFrameReceived(frame);
      });

  nghttp2_session_callbacks_set_on_stream_close_callback(
      callbacks_,
      [](nghttp2_session*, int32_t stream_id, uint32_t error_code, void* user_data) -> int {
        return static_cast<ConnectionImpl*>(user_data)->onStreamClose(stream_id, error_code);
      });

  nghttp2_session_callbacks_set_on_frame_send_callback(
      callbacks_, [](nghttp2_session*, const nghttp2_frame* frame, void* user_data) -> int {
        return static_cast<ConnectionImpl*>(user_data)->onFrameSend(frame);
      });

  nghttp2_session_callbacks_set_on_frame_not_send_callback(
      callbacks_, [](nghttp2_session*, const nghttp2_frame*, int, void*) -> int {
        // We used to always return failure here but it looks now this can get called if the other
        // side sends GOAWAY and we are trying to send a SETTINGS ACK. Just ignore this for now.
        return 0;
      });

  nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(
      callbacks_,
      [](nghttp2_session*, const nghttp2_frame*, int error_code, void* user_data) -> int {
        return static_cast<ConnectionImpl*>(user_data)->onInvalidFrame(error_code);
      });
}

ConnectionImpl::Http2Callbacks::~Http2Callbacks() { nghttp2_session_callbacks_del(callbacks_); }

ConnectionImpl::Http2Options::Http2Options() {
  nghttp2_option_new(&options_);
  // Currently we do not do anything with stream priority. Setting the following option prevents
  // nghttp2 from keeping around closed streams for use during stream priority dependency graph
  // calculations. This saves a tremendous amount of memory in cases where there are a large number
  // of kept alive HTTP/2 connections.
  nghttp2_option_set_no_closed_streams(options_, 1);
  nghttp2_option_set_no_auto_window_update(options_, 1);
}

ConnectionImpl::Http2Options::~Http2Options() { nghttp2_option_del(options_); }

ClientConnectionImpl::ClientConnectionImpl(Network::Connection& connection,
                                           Http::ConnectionCallbacks& callbacks,
                                           Stats::Scope& stats, const Http2Settings& http2_settings)
    : ConnectionImpl(connection, stats, http2_settings), callbacks_(callbacks) {
  nghttp2_session_client_new2(&session_, http2_callbacks_.callbacks(), base(),
                              http2_options_.options());
  sendSettings(http2_settings, true);
}

Http::StreamEncoder& ClientConnectionImpl::newStream(StreamDecoder& decoder) {
  StreamImplPtr stream(new ClientStreamImpl(*this, per_stream_buffer_limit_));
  stream->decoder_ = &decoder;
  // If the connection is currently above the high watermark, make sure to inform the new stream.
  // The connection can not pass this on automatically as it has no awareness that a new stream is
  // created.
  if (connection_.aboveHighWatermark()) {
    stream->runHighWatermarkCallbacks();
  }
  stream->moveIntoList(std::move(stream), active_streams_);
  return *active_streams_.front();
}

int ClientConnectionImpl::onBeginHeaders(const nghttp2_frame* frame) {
  // The client code explicitly does not currently suport push promise.
  RELEASE_ASSERT(frame->hd.type == NGHTTP2_HEADERS);
  RELEASE_ASSERT(frame->headers.cat == NGHTTP2_HCAT_RESPONSE ||
                 frame->headers.cat == NGHTTP2_HCAT_HEADERS);
  if (frame->headers.cat == NGHTTP2_HCAT_HEADERS) {
    StreamImpl* stream = getStream(frame->hd.stream_id);
    ASSERT(!stream->headers_);
    stream->headers_.reset(new HeaderMapImpl());
  }

  return 0;
}

int ClientConnectionImpl::onHeader(const nghttp2_frame* frame, HeaderString&& name,
                                   HeaderString&& value) {
  // The client code explicitly does not currently suport push promise.
  ASSERT(frame->hd.type == NGHTTP2_HEADERS);
  ASSERT(frame->headers.cat == NGHTTP2_HCAT_RESPONSE || frame->headers.cat == NGHTTP2_HCAT_HEADERS);
  return saveHeader(frame, std::move(name), std::move(value));
}

ServerConnectionImpl::ServerConnectionImpl(Network::Connection& connection,
                                           Http::ServerConnectionCallbacks& callbacks,
                                           Stats::Scope& scope, const Http2Settings& http2_settings)
    : ConnectionImpl(connection, scope, http2_settings), callbacks_(callbacks) {
  nghttp2_session_server_new2(&session_, http2_callbacks_.callbacks(), base(),
                              http2_options_.options());
  sendSettings(http2_settings, false);
}

int ServerConnectionImpl::onBeginHeaders(const nghttp2_frame* frame) {
  // For a server connection, we should never get push promise frames.
  ASSERT(frame->hd.type == NGHTTP2_HEADERS);
  if (frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
    stats_.trailers_.inc();
    ASSERT(frame->headers.cat == NGHTTP2_HCAT_HEADERS);

    StreamImpl* stream = getStream(frame->hd.stream_id);
    ASSERT(!stream->headers_);
    stream->headers_.reset(new HeaderMapImpl());
    return 0;
  }

  StreamImplPtr stream(new ServerStreamImpl(*this, per_stream_buffer_limit_));
  if (connection_.aboveHighWatermark()) {
    stream->runHighWatermarkCallbacks();
  }
  stream->decoder_ = &callbacks_.newStream(*stream);
  stream->stream_id_ = frame->hd.stream_id;
  stream->moveIntoList(std::move(stream), active_streams_);
  nghttp2_session_set_stream_user_data(session_, frame->hd.stream_id,
                                       active_streams_.front().get());
  return 0;
}

int ServerConnectionImpl::onHeader(const nghttp2_frame* frame, HeaderString&& name,
                                   HeaderString&& value) {
  // For a server connection, we should never get push promise frames.
  ASSERT(frame->hd.type == NGHTTP2_HEADERS);
  ASSERT(frame->headers.cat == NGHTTP2_HCAT_REQUEST || frame->headers.cat == NGHTTP2_HCAT_HEADERS);
  return saveHeader(frame, std::move(name), std::move(value));
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
