#include "codec_impl.h"

#include "envoy/event/dispatcher.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"

namespace Http {
namespace Http2 {

void Utility::reconstituteCrumbledCookies(HeaderMap& headers) {
  std::string final_cookies;
  bool replace_cookies = false;
  headers.iterate([&](const LowerCaseString& key, const std::string& value) -> void {
    if (key == Headers::get().Cookie) {
      if (final_cookies.empty()) {
        final_cookies = value;
      } else {
        final_cookies += "; " + value;
        replace_cookies = true;
      }
    }
  });

  if (replace_cookies) {
    headers.replaceViaMoveValue(Headers::get().Cookie, std::move(final_cookies));
  }
}

ConnectionImpl::Http2Callbacks ConnectionImpl::http2_callbacks_;

/**
 * Helper to remove const during a cast. nghttp2 takes non-const pointers for headers even though
 * it copies them.
 */
template <typename T> static T* remove_const(const void* object) {
  return const_cast<T*>(reinterpret_cast<const T*>(object));
}

ConnectionImpl::StreamImpl::StreamImpl(ConnectionImpl& parent)
    : parent_(parent), headers_(new HeaderMapImpl()) {}

ConnectionImpl::StreamImpl::~StreamImpl() {}

void ConnectionImpl::StreamImpl::buildHeaders(std::vector<nghttp2_nv>& final_headers,
                                              const HeaderMap& headers) {
  // nghttp2 requires that all ':' headers come before all other headers. To avoid making higher
  // layers understand that we do two passes here to build the final header list to encode.
  final_headers.reserve(headers.size());
  headers.iterate([&](const LowerCaseString& key, const std::string& value) -> void {
    if (key.get()[0] == ':') {
      final_headers.push_back({remove_const<uint8_t>(key.get().c_str()),
                               remove_const<uint8_t>(value.c_str()), key.get().size(), value.size(),
                               0});
    }
  });

  headers.iterate([&](const LowerCaseString& key, const std::string& value) -> void {
    if (key.get()[0] != ':') {
      final_headers.push_back({remove_const<uint8_t>(key.get().c_str()),
                               remove_const<uint8_t>(value.c_str()), key.get().size(), value.size(),
                               0});
    }
  });
}

void ConnectionImpl::StreamImpl::encodeHeaders(const HeaderMap& headers, bool end_stream) {
  std::vector<nghttp2_nv> final_headers;
  buildHeaders(final_headers, headers);

  nghttp2_data_provider provider;
  if (!end_stream) {
    provider.source.ptr = this;
    provider.read_callback =
        [](nghttp2_session*, int32_t, uint8_t*, size_t length, uint32_t* data_flags,
           nghttp2_data_source* source, void*) -> ssize_t {
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

void ConnectionImpl::StreamImpl::submitTrailers(const HeaderMap& trailers) {
  std::vector<nghttp2_nv> final_headers;
  buildHeaders(final_headers, trailers);
  int rc =
      nghttp2_submit_trailer(parent_.session_, stream_id_, &final_headers[0], final_headers.size());
  ASSERT(rc == 0);
  UNREFERENCED_PARAMETER(rc);
}

ssize_t ConnectionImpl::StreamImpl::onDataSourceRead(size_t length, uint32_t* data_flags) {
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

  // TODO: Back pressure.
  uint64_t length_remaining = length;
  Buffer::OwnedImpl output(framehd, FRAME_HEADER_SIZE);
  uint64_t num_slices = pending_send_data_.getRawSlices(nullptr, 0);
  Buffer::RawSlice slices[num_slices];
  pending_send_data_.getRawSlices(slices, num_slices);
  for (Buffer::RawSlice& slice : slices) {
    if (length_remaining == 0) {
      break;
    }

    uint64_t data_to_write = std::min(length_remaining, slice.len_);
    output.add(slice.mem_, data_to_write);
    length_remaining -= data_to_write;
  }

  parent_.connection_.write(output);
  pending_send_data_.drain(length);
  return 0;
}

void ConnectionImpl::ClientStreamImpl::submitHeaders(const std::vector<nghttp2_nv>& final_headers,
                                                     nghttp2_data_provider* provider) {
  ASSERT(stream_id_ == -1);
  stream_id_ = nghttp2_submit_request(parent_.session_, nullptr, &final_headers[0],
                                      final_headers.size(), provider, base());
  ASSERT(stream_id_ > 0);
}

void ConnectionImpl::ServerStreamImpl::submitHeaders(const std::vector<nghttp2_nv>& final_headers,
                                                     nghttp2_data_provider* provider) {
  ASSERT(stream_id_ != -1);
  int rc = nghttp2_submit_response(parent_.session_, stream_id_, &final_headers[0],
                                   final_headers.size(), provider);
  ASSERT(rc == 0);
  UNREFERENCED_PARAMETER(rc);
}

void ConnectionImpl::StreamImpl::encodeData(const Buffer::Instance& data, bool end_stream) {
  ASSERT(!local_end_stream_);
  local_end_stream_ = end_stream;
  pending_send_data_.add(data);
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
    deferred_reset_.value(reason);
  } else {
    resetStreamWorker(reason);
    parent_.sendPendingFrames();
  }
}

void ConnectionImpl::StreamImpl::resetStreamWorker(StreamResetReason reason) {
  int rc = nghttp2_submit_rst_stream(parent_.session_, NGHTTP2_FLAG_NONE, stream_id_,
                                     reason == StreamResetReason::LocalRefusedStreamReset
                                         ? NGHTTP2_REFUSED_STREAM
                                         : NGHTTP2_NO_ERROR);
  ASSERT(rc == 0);
  UNREFERENCED_PARAMETER(rc);
}

void ConnectionImpl::StreamImpl::runResetCallbacks(StreamResetReason reason) {
  if (reset_callbacks_run_) {
    return;
  }

  for (StreamCallbacks* callbacks : callbacks_) {
    callbacks->onResetStream(reason);
  }

  reset_callbacks_run_ = true;
}

ConnectionImpl::~ConnectionImpl() { nghttp2_session_del(session_); }

void ConnectionImpl::dispatch(Buffer::Instance& data) {
  conn_log_trace("dispatching {} bytes", connection_, data.length());
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

  conn_log_trace("dispatched {} bytes", connection_, data.length());
  data.drain(data.length());

  // Decoding incoming frames can generate outbound frames so flush pending.
  sendPendingFrames();
}

ConnectionImpl::StreamImpl* ConnectionImpl::getStream(int32_t stream_id) {
  return static_cast<StreamImpl*>(nghttp2_session_get_stream_user_data(session_, stream_id));
}

int ConnectionImpl::onData(int32_t stream_id, const uint8_t* data, size_t len) {
  getStream(stream_id)->pending_recv_data_.add(data, len);
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
  conn_log_trace("recv frame type={}", connection_, static_cast<uint64_t>(frame->hd.type));

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
    Utility::reconstituteCrumbledCookies(*stream->headers_);
    if (frame->headers.cat == NGHTTP2_HCAT_REQUEST || frame->headers.cat == NGHTTP2_HCAT_RESPONSE) {
      stream->headers_->addViaCopy(Headers::get().Version, PROTOCOL_STRING);
      stream->decoder_->decodeHeaders(std::move(stream->headers_), stream->remote_end_stream_);
    } else {
      ASSERT(frame->headers.cat == NGHTTP2_HCAT_HEADERS);
      ASSERT(stream->remote_end_stream_);

      // It's possible that we are waiting to send a deferred reset, so only raise trailers if local
      // is not complete.
      if (!stream->deferred_reset_.valid()) {
        stream->decoder_->decodeTrailers(std::move(stream->headers_));
      }
    }

    stream->headers_.reset();
    break;
  }
  case NGHTTP2_DATA: {
    stream->remote_end_stream_ = frame->hd.flags & NGHTTP2_FLAG_END_STREAM;

    // It's possible that we are waiting to send a deferred reset, so only raise data if local
    // is not complete.
    if (!stream->deferred_reset_.valid()) {
      stream->decoder_->decodeData(stream->pending_recv_data_, stream->remote_end_stream_);
    }

    stream->pending_recv_data_.drain(stream->pending_recv_data_.length());
    break;
  }
  case NGHTTP2_RST_STREAM: {
    conn_log_trace("remote reset: {}", connection_, frame->rst_stream.error_code);
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
  conn_log_trace("sent frame type={}", connection_, static_cast<uint64_t>(frame->hd.type));
  switch (frame->hd.type) {
  case NGHTTP2_GOAWAY: {
    if (frame->goaway.error_code != NGHTTP2_NO_ERROR) {
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    break;
  }

  case NGHTTP2_RST_STREAM: {
    conn_log_debug("sent reset code={}", connection_, frame->rst_stream.error_code);
    stats_.tx_reset_.inc();
    break;
  }

  case NGHTTP2_HEADERS:
  case NGHTTP2_DATA: {
    StreamImpl* stream = getStream(frame->hd.stream_id);
    stream->local_end_stream_sent_ = frame->hd.flags & NGHTTP2_FLAG_END_STREAM;
    if (stream->local_end_stream_sent_ && stream->deferred_reset_.valid()) {
      stream->resetStreamWorker(stream->deferred_reset_.value());
    }
    break;
  }
  }

  return 0;
}

int ConnectionImpl::onInvalidFrame(int error_code) {
  UNREFERENCED_PARAMETER(error_code);

  conn_log_debug("invalid frame: {}", connection_, nghttp2_strerror(error_code));
  // Cause dispatch to return with an error code.
  return NGHTTP2_ERR_CALLBACK_FAILURE;
}

ssize_t ConnectionImpl::onSend(const uint8_t* data, size_t length) {
  // TODO: Back pressure.
  conn_log_trace("send data: bytes={}", connection_, length);
  Buffer::OwnedImpl buffer(data, length);
  connection_.write(buffer);
  return length;
}

int ConnectionImpl::onStreamClose(int32_t stream_id, uint32_t error_code) {
  UNREFERENCED_PARAMETER(error_code);

  StreamImpl* stream = getStream(stream_id);
  if (stream) {
    conn_log_debug("stream closed: {}", connection_, error_code);
    if (!stream->remote_end_stream_ || !stream->local_end_stream_) {
      stream->runResetCallbacks(error_code == NGHTTP2_REFUSED_STREAM
                                    ? StreamResetReason::RemoteRefusedStreamReset
                                    : StreamResetReason::RemoteReset);
    }

    connection_.dispatcher().deferredDelete(stream->removeFromList(active_streams_));
    nghttp2_session_set_stream_user_data(session_, stream->stream_id_, nullptr);
  }

  return 0;
}

int ConnectionImpl::saveHeader(const nghttp2_frame* frame, std::string&& name,
                               std::string&& value) {
  StreamImpl* stream = getStream(frame->hd.stream_id);
  if (!stream) {
    // We have seen 1 or 2 crashes where we get a headers callback but there is no associated
    // stream data. I honestly am not sure how this can happen. However, from reading the nghttp2
    // code it looks possible that inflate_header_block() can safely inflate headers for an already
    // closed stream, but will still call the headers callback. Since that seems possible, we should
    // ignore this case here.
    // TODO: Figure out a test case that can hit this.
    stats_.headers_cb_no_stream_.inc();
    return 0;
  }

  stream->headers_->addViaMove(LowerCaseString(std::move(name), false), std::move(value));
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
}

void ConnectionImpl::sendSettings(uint64_t codec_options) {
  std::vector<nghttp2_settings_entry> iv = {
      {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, MAX_CONCURRENT_STREAMS},
      {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, DEFAULT_WINDOW_SIZE}};

  if (codec_options & CodecOptions::NoCompression) {
    iv.push_back({NGHTTP2_SETTINGS_HEADER_TABLE_SIZE, 0});
    conn_log_debug("disabling header compression", connection_);
  }

  int rc = nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE, &iv[0], iv.size());
  ASSERT(rc == 0);
  UNREFERENCED_PARAMETER(rc);

  // Increase connection window size up to our default size.
  rc = nghttp2_submit_window_update(session_, NGHTTP2_FLAG_NONE, 0,
                                    DEFAULT_WINDOW_SIZE - NGHTTP2_INITIAL_CONNECTION_WINDOW_SIZE);
  ASSERT(rc == 0);
}

ConnectionImpl::Http2Callbacks::Http2Callbacks() {
  nghttp2_session_callbacks_new(&callbacks_);
  nghttp2_session_callbacks_set_send_callback(
      callbacks_,
      [](nghttp2_session*, const uint8_t* data, size_t length, int, void* user_data)
          -> ssize_t { return static_cast<ConnectionImpl*>(user_data)->onSend(data, length); });

  nghttp2_session_callbacks_set_send_data_callback(
      callbacks_, [](nghttp2_session*, nghttp2_frame* frame, const uint8_t* framehd, size_t length,
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

        std::string name(reinterpret_cast<const char*>(raw_name), name_length);
        std::string value(reinterpret_cast<const char*>(raw_value), value_length);
        return static_cast<ConnectionImpl*>(user_data)
            ->onHeader(frame, std::move(name), std::move(value));
      });

  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
      callbacks_, [](nghttp2_session*, uint8_t, int32_t stream_id, const uint8_t* data, size_t len,
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
      [](nghttp2_session*, const nghttp2_frame*, int error_code, void* user_data)
          -> int { return static_cast<ConnectionImpl*>(user_data)->onInvalidFrame(error_code); });
}

ConnectionImpl::Http2Callbacks::~Http2Callbacks() { nghttp2_session_callbacks_del(callbacks_); }

ClientConnectionImpl::ClientConnectionImpl(Network::Connection& connection,
                                           ConnectionCallbacks& callbacks, Stats::Store& stats,
                                           uint64_t codec_options)
    : ConnectionImpl(connection, stats), callbacks_(callbacks) {
  nghttp2_session_client_new(&session_, http2_callbacks_.callbacks(), base());
  sendSettings(codec_options);
}

Http::StreamEncoder& ClientConnectionImpl::newStream(StreamDecoder& decoder) {
  StreamImplPtr stream(new ClientStreamImpl(*this));
  stream->decoder_ = &decoder;
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

int ClientConnectionImpl::onHeader(const nghttp2_frame* frame, std::string&& name,
                                   std::string&& value) {
  // The client code explicitly does not currently suport push promise.
  ASSERT(frame->hd.type == NGHTTP2_HEADERS);
  ASSERT(frame->headers.cat == NGHTTP2_HCAT_RESPONSE || frame->headers.cat == NGHTTP2_HCAT_HEADERS);
  return saveHeader(frame, std::move(name), std::move(value));
}

ServerConnectionImpl::ServerConnectionImpl(Network::Connection& connection,
                                           Http::ServerConnectionCallbacks& callbacks,
                                           Stats::Store& stats, uint64_t codec_options)
    : ConnectionImpl(connection, stats), callbacks_(callbacks) {
  nghttp2_session_server_new(&session_, http2_callbacks_.callbacks(), base());
  sendSettings(codec_options);
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

  StreamImplPtr stream(new ServerStreamImpl(*this));
  stream->decoder_ = &callbacks_.newStream(*stream);
  stream->stream_id_ = frame->hd.stream_id;
  stream->moveIntoList(std::move(stream), active_streams_);
  nghttp2_session_set_stream_user_data(session_, frame->hd.stream_id,
                                       active_streams_.front().get());
  return 0;
}

int ServerConnectionImpl::onHeader(const nghttp2_frame* frame, std::string&& name,
                                   std::string&& value) {
  // For a server connection, we should never get push promise frames.
  ASSERT(frame->hd.type == NGHTTP2_HEADERS);
  ASSERT(frame->headers.cat == NGHTTP2_HCAT_REQUEST || frame->headers.cat == NGHTTP2_HCAT_HEADERS);
  return saveHeader(frame, std::move(name), std::move(value));
}

} // Http2
} // Http
