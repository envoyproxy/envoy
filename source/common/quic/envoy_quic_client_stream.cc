#include "common/quic/envoy_quic_client_stream.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/spdy/core/spdy_header_block.h"
#include "common/quic/platform/quic_mem_slice_span_impl.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "common/quic/envoy_quic_utils.h"
#include "common/quic/envoy_quic_client_session.h"

#include "common/buffer/buffer_impl.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/header_utility.h"
#include "common/http/utility.h"
#include "common/common/enum_to_int.h"
#include "common/common/assert.h"

namespace Envoy {
namespace Quic {

EnvoyQuicClientStream::EnvoyQuicClientStream(
    quic::QuicStreamId id, quic::QuicSpdyClientSession* client_session, quic::StreamType type,
    Http::Http3::CodecStats& stats,
    const envoy::config::core::v3::Http3ProtocolOptions& http3_options)
    : quic::QuicSpdyClientStream(id, client_session, type),
      EnvoyQuicStream(
          // Flow control receive window should be larger than 8k so that the send buffer can fully
          // utilize congestion control window before it reaches the high watermark.
          static_cast<uint32_t>(GetReceiveWindow().value()), *filterManagerConnection(),
          [this]() { runLowWatermarkCallbacks(); }, [this]() { runHighWatermarkCallbacks(); },
          stats, http3_options) {
  ASSERT(GetReceiveWindow() > 8 * 1024, "Send buffer limit should be larger than 8KB.");
}

EnvoyQuicClientStream::EnvoyQuicClientStream(
    quic::PendingStream* pending, quic::QuicSpdyClientSession* client_session,
    quic::StreamType type, Http::Http3::CodecStats& stats,
    const envoy::config::core::v3::Http3ProtocolOptions& http3_options)
    : quic::QuicSpdyClientStream(pending, client_session, type),
      EnvoyQuicStream(
          static_cast<uint32_t>(GetReceiveWindow().value()), *filterManagerConnection(),
          [this]() { runLowWatermarkCallbacks(); }, [this]() { runHighWatermarkCallbacks(); },
          stats, http3_options) {}

Http::Status EnvoyQuicClientStream::encodeHeaders(const Http::RequestHeaderMap& headers,
                                                  bool end_stream) {
  // Required headers must be present. This can only happen by some erroneous processing after the
  // downstream codecs decode.
  RETURN_IF_ERROR(Http::HeaderUtility::checkRequiredHeaders(headers));

  ENVOY_STREAM_LOG(debug, "encodeHeaders: (end_stream={}) {}.", *this, end_stream, headers);
  local_end_stream_ = end_stream;
  SendBufferMonitor::ScopedWatermarkBufferUpdater updater(this, this);
  auto spdy_headers = envoyHeadersToSpdyHeaderBlock(headers);
  if (headers.Method() && headers.Method()->value() == "CONNECT") {
    // It is a bytestream connect and should have :path and :protocol set accordingly
    // As HTTP/1.1 does not require a path for CONNECT, we may have to add one
    // if shifting codecs. For now, default to "/" - this can be made
    // configurable if necessary.
    // https://tools.ietf.org/html/draft-kinnear-httpbis-http2-transport-02
    spdy_headers[":protocol"] = Http::Headers::get().ProtocolValues.Bytestream;
    if (!headers.Path()) {
      spdy_headers[":path"] = "/";
    }
  }
  WriteHeaders(std::move(spdy_headers), end_stream, nullptr);
  return Http::okStatus();
}

void EnvoyQuicClientStream::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_STREAM_LOG(debug, "encodeData (end_stream={}) of {} bytes.", *this, end_stream,
                   data.length());
  if (data.length() == 0 && !end_stream) {
    return;
  }
  ASSERT(!local_end_stream_);
  local_end_stream_ = end_stream;
  SendBufferMonitor::ScopedWatermarkBufferUpdater updater(this, this);
  // QUIC stream must take all.
  WriteBodySlices(quic::QuicMemSliceSpan(quic::QuicMemSliceSpanImpl(data)), end_stream);
  if (data.length() > 0) {
    // Send buffer didn't take all the data, threshold needs to be adjusted.
    Reset(quic::QUIC_BAD_APPLICATION_PAYLOAD);
    return;
  }
}

void EnvoyQuicClientStream::encodeTrailers(const Http::RequestTrailerMap& trailers) {
  ASSERT(!local_end_stream_);
  local_end_stream_ = true;
  ENVOY_STREAM_LOG(debug, "encodeTrailers: {}.", *this, trailers);
  ScopedWatermarkBufferUpdater updater(this, this);
  WriteTrailers(envoyHeadersToSpdyHeaderBlock(trailers), nullptr);
}

void EnvoyQuicClientStream::encodeMetadata(const Http::MetadataMapVector& /*metadata_map_vector*/) {
  // Metadata Frame is not supported in QUIC.
  // TODO(danzh): add stats for metadata not supported error.
}

void EnvoyQuicClientStream::resetStream(Http::StreamResetReason reason) {
  Reset(envoyResetReasonToQuicRstError(reason));
}

void EnvoyQuicClientStream::switchStreamBlockState(bool should_block) {
  if (should_block) {
    sequencer()->SetBlockedUntilFlush();
  } else {
    ASSERT(read_disable_counter_ == 0, "readDisable called in between.");
    sequencer()->SetUnblocked();
  }
}

void EnvoyQuicClientStream::OnInitialHeadersComplete(bool fin, size_t frame_len,
                                                     const quic::QuicHeaderList& header_list) {
  if (read_side_closed()) {
    return;
  }
  quic::QuicSpdyStream::OnInitialHeadersComplete(fin, frame_len, header_list);
  if (!headers_decompressed() || header_list.empty()) {
    onStreamError(!http3_options_.override_stream_error_on_invalid_http_message().value());
    return;
  }

  ENVOY_STREAM_LOG(debug, "Received headers: {}.", *this, header_list.DebugString());
  if (fin) {
    end_stream_decoded_ = true;
  }
  std::unique_ptr<Http::ResponseHeaderMapImpl> headers =
      quicHeadersToEnvoyHeaders<Http::ResponseHeaderMapImpl>(header_list, *this);
  if (headers == nullptr) {
    onStreamError(close_connection_upon_invalid_header_);
    return;
  }
  const absl::optional<uint64_t> optional_status =
      Http::Utility::getResponseStatusNoThrow(*headers);
  if (!optional_status.has_value()) {
    details_ = Http3ResponseCodeDetailValues::invalid_http_header;
    onStreamError(!http3_options_.override_stream_error_on_invalid_http_message().value());
    return;
  }
  const uint64_t status = optional_status.value();
  if (Http::CodeUtility::is1xx(status)) {
    if (status == enumToInt(Http::Code::SwitchingProtocols)) {
      // HTTP3 doesn't support the HTTP Upgrade mechanism or 101 (Switching Protocols) status code.
      Reset(quic::QUIC_BAD_APPLICATION_PAYLOAD);
      return;
    }

    // These are Informational 1xx headers, not the actual response headers.
    set_headers_decompressed(false);
  }

  if (status == enumToInt(Http::Code::Continue) && !decoded_100_continue_) {
    // This is 100 Continue, only decode it once to support Expect:100-Continue header.
    decoded_100_continue_ = true;
    response_decoder_->decode100ContinueHeaders(std::move(headers));
  } else if (status != enumToInt(Http::Code::Continue)) {
    response_decoder_->decodeHeaders(std::move(headers),
                                     /*end_stream=*/fin);
  }

  ConsumeHeaderList();
}

void EnvoyQuicClientStream::OnBodyAvailable() {
  ASSERT(FinishedReadingHeaders());
  ASSERT(read_disable_counter_ == 0);
  ASSERT(!in_decode_data_callstack_);
  if (read_side_closed()) {
    return;
  }
  in_decode_data_callstack_ = true;

  Buffer::InstancePtr buffer = std::make_unique<Buffer::OwnedImpl>();
  // TODO(danzh): check Envoy per stream buffer limit.
  // Currently read out all the data.
  while (HasBytesToRead()) {
    iovec iov;
    int num_regions = GetReadableRegions(&iov, 1);
    ASSERT(num_regions > 0);
    size_t bytes_read = iov.iov_len;
    buffer->add(iov.iov_base, bytes_read);
    MarkConsumed(bytes_read);
  }
  ASSERT(buffer->length() == 0 || !end_stream_decoded_);

  bool fin_read_and_no_trailers = IsDoneReading();
  // If this call is triggered by an empty frame with FIN which is not from peer
  // but synthesized by stream itself upon receiving HEADERS with FIN or
  // TRAILERS, do not deliver end of stream here. Because either decodeHeaders
  // already delivered it or decodeTrailers will be called.
  bool skip_decoding = (buffer->length() == 0 && !fin_read_and_no_trailers) || end_stream_decoded_;
  if (!skip_decoding) {
    if (fin_read_and_no_trailers) {
      end_stream_decoded_ = true;
    }
    response_decoder_->decodeData(*buffer, fin_read_and_no_trailers);
  }

  if (!sequencer()->IsClosed() || read_side_closed()) {
    in_decode_data_callstack_ = false;
    if (read_disable_counter_ > 0) {
      // If readDisable() was ever called during decodeData() and it meant to disable
      // reading from downstream, the call must have been deferred. Call it now.
      switchStreamBlockState(true);
    }
    return;
  }

  // Trailers may arrived earlier and wait to be consumed after reading all the body. Consume it
  // here.
  maybeDecodeTrailers();

  OnFinRead();
  in_decode_data_callstack_ = false;
}

void EnvoyQuicClientStream::OnTrailingHeadersComplete(bool fin, size_t frame_len,
                                                      const quic::QuicHeaderList& header_list) {
  if (read_side_closed()) {
    return;
  }
  ENVOY_STREAM_LOG(debug, "Received trailers: {}.", *this, header_list.DebugString());
  quic::QuicSpdyStream::OnTrailingHeadersComplete(fin, frame_len, header_list);
  ASSERT(trailers_decompressed());
  if (session()->connection()->connected() && !rst_sent()) {
    maybeDecodeTrailers();
  }
}

void EnvoyQuicClientStream::maybeDecodeTrailers() {
  if (sequencer()->IsClosed() && !FinishedReadingTrailers()) {
    // Only decode trailers after finishing decoding body.
    end_stream_decoded_ = true;
    response_decoder_->decodeTrailers(
        spdyHeaderBlockToEnvoyHeaders<Http::ResponseTrailerMapImpl>(received_trailers()));
    MarkTrailersConsumed();
  }
}

void EnvoyQuicClientStream::OnStreamReset(const quic::QuicRstStreamFrame& frame) {
  quic::QuicSpdyClientStream::OnStreamReset(frame);
  runResetCallbacks(quicRstErrorToEnvoyRemoteResetReason(frame.error_code));
}

void EnvoyQuicClientStream::Reset(quic::QuicRstStreamErrorCode error) {
  // Upper layers expect calling resetStream() to immediately raise reset callbacks.
  runResetCallbacks(quicRstErrorToEnvoyLocalResetReason(error));
  quic::QuicSpdyClientStream::Reset(error);
}

void EnvoyQuicClientStream::OnConnectionClosed(quic::QuicErrorCode error,
                                               quic::ConnectionCloseSource source) {
  if (!end_stream_decoded_) {
    runResetCallbacks(source == quic::ConnectionCloseSource::FROM_SELF
                          ? quicErrorCodeToEnvoyLocalResetReason(error)
                          : quicErrorCodeToEnvoyRemoteResetReason(error));
  }
  quic::QuicSpdyClientStream::OnConnectionClosed(error, source);
}

void EnvoyQuicClientStream::OnClose() {
  quic::QuicSpdyClientStream::OnClose();
  if (isDoingWatermarkAccounting()) {
    // This is called in the scope of a watermark buffer updater. Clear the
    // buffer accounting afterwards so that the updater doesn't override the
    // result.
    connection()->dispatcher().post([this] { clearWatermarkBuffer(); });
    return;
  }
  clearWatermarkBuffer();
}

void EnvoyQuicClientStream::clearWatermarkBuffer() {
  if (BufferedDataBytes() > 0) {
    // If the stream is closed without sending out all buffered data, regard
    // them as sent now and adjust connection buffer book keeping.
    updateBytesBuffered(BufferedDataBytes(), 0);
  }
}

void EnvoyQuicClientStream::OnCanWrite() {
  SendBufferMonitor::ScopedWatermarkBufferUpdater updater(this, this);
  quic::QuicSpdyClientStream::OnCanWrite();
}

uint32_t EnvoyQuicClientStream::streamId() { return id(); }

Network::Connection* EnvoyQuicClientStream::connection() { return filterManagerConnection(); }

QuicFilterManagerConnectionImpl* EnvoyQuicClientStream::filterManagerConnection() {
  return dynamic_cast<QuicFilterManagerConnectionImpl*>(session());
}

void EnvoyQuicClientStream::onStreamError(absl::optional<bool> should_close_connection) {
  if (details_.empty()) {
    details_ = Http3ResponseCodeDetailValues::invalid_http_header;
  }
  bool close_connection_upon_invalid_header;
  if (should_close_connection != absl::nullopt) {
    close_connection_upon_invalid_header = should_close_connection.value();
  } else {
    close_connection_upon_invalid_header =
        !http3_options_.override_stream_error_on_invalid_http_message().value();
  }
  if (close_connection_upon_invalid_header) {
    stream_delegate()->OnStreamError(quic::QUIC_HTTP_FRAME_ERROR, "Invalid headers");
  } else {
    Reset(quic::QUIC_BAD_APPLICATION_PAYLOAD);
  }
}

} // namespace Quic
} // namespace Envoy
