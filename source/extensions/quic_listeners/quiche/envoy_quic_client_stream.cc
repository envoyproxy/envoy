#include "extensions/quic_listeners/quiche/envoy_quic_client_stream.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/spdy/core/spdy_header_block.h"
#include "extensions/quic_listeners/quiche/platform/quic_mem_slice_span_impl.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"
#include "extensions/quic_listeners/quiche/envoy_quic_client_session.h"

#include "common/buffer/buffer_impl.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/header_utility.h"
#include "common/http/utility.h"
#include "common/common/enum_to_int.h"
#include "common/common/assert.h"

namespace Envoy {
namespace Quic {

EnvoyQuicClientStream::EnvoyQuicClientStream(quic::QuicStreamId id,
                                             quic::QuicSpdyClientSession* client_session,
                                             quic::StreamType type)
    : quic::QuicSpdyClientStream(id, client_session, type),
      EnvoyQuicStream(
          // This should be larger than 8k to fully utilize congestion control
          // window. And no larger than the max stream flow control window for
          // the stream to buffer all the data.
          // Ideally this limit should also correlate to peer's receive window
          // but not fully depends on that.
          16 * 1024, [this]() { runLowWatermarkCallbacks(); },
          [this]() { runHighWatermarkCallbacks(); }) {}

EnvoyQuicClientStream::EnvoyQuicClientStream(quic::PendingStream* pending,
                                             quic::QuicSpdyClientSession* client_session,
                                             quic::StreamType type)
    : quic::QuicSpdyClientStream(pending, client_session, type),
      EnvoyQuicStream(
          16 * 1024, [this]() { runLowWatermarkCallbacks(); },
          [this]() { runHighWatermarkCallbacks(); }) {}

Http::Status EnvoyQuicClientStream::encodeHeaders(const Http::RequestHeaderMap& headers,
                                                  bool end_stream) {
  // Required headers must be present. This can only happen by some erroneous processing after the
  // downstream codecs decode.
  RETURN_IF_ERROR(Http::HeaderUtility::checkRequiredHeaders(headers));

  ENVOY_STREAM_LOG(debug, "encodeHeaders: (end_stream={}) {}.", *this, end_stream, headers);
  quic::QuicStream* writing_stream =
      quic::VersionUsesHttp3(transport_version())
          ? static_cast<quic::QuicStream*>(this)
          : (dynamic_cast<quic::QuicSpdySession*>(session())->headers_stream());
  const uint64_t bytes_to_send_old = writing_stream->BufferedDataBytes();
  WriteHeaders(envoyHeadersToSpdyHeaderBlock(headers), end_stream, nullptr);
  local_end_stream_ = end_stream;
  const uint64_t bytes_to_send_new = writing_stream->BufferedDataBytes();
  ASSERT(bytes_to_send_old <= bytes_to_send_new);
  // IETF QUIC sends HEADER frame on current stream. After writing headers, the
  // buffer may increase.
  maybeCheckWatermark(bytes_to_send_old, bytes_to_send_new, *filterManagerConnection());
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
  // This is counting not serialized bytes in the send buffer.
  const uint64_t bytes_to_send_old = BufferedDataBytes();
  // QUIC stream must take all.
  WriteBodySlices(quic::QuicMemSliceSpan(quic::QuicMemSliceSpanImpl(data)), end_stream);
  if (data.length() > 0) {
    // Send buffer didn't take all the data, threshold needs to be adjusted.
    Reset(quic::QUIC_BAD_APPLICATION_PAYLOAD);
    return;
  }

  const uint64_t bytes_to_send_new = BufferedDataBytes();
  ASSERT(bytes_to_send_old <= bytes_to_send_new);
  maybeCheckWatermark(bytes_to_send_old, bytes_to_send_new, *filterManagerConnection());
}

void EnvoyQuicClientStream::encodeTrailers(const Http::RequestTrailerMap& trailers) {
  ASSERT(!local_end_stream_);
  local_end_stream_ = true;
  ENVOY_STREAM_LOG(debug, "encodeTrailers: {}.", *this, trailers);
  quic::QuicStream* writing_stream =
      quic::VersionUsesHttp3(transport_version())
          ? static_cast<quic::QuicStream*>(this)
          : (dynamic_cast<quic::QuicSpdySession*>(session())->headers_stream());

  const uint64_t bytes_to_send_old = writing_stream->BufferedDataBytes();
  WriteTrailers(envoyHeadersToSpdyHeaderBlock(trailers), nullptr);
  const uint64_t bytes_to_send_new = writing_stream->BufferedDataBytes();
  ASSERT(bytes_to_send_old <= bytes_to_send_new);
  // IETF QUIC sends HEADER frame on current stream. After writing trailers, the
  // buffer may increase.
  maybeCheckWatermark(bytes_to_send_old, bytes_to_send_new, *filterManagerConnection());
}

void EnvoyQuicClientStream::encodeMetadata(const Http::MetadataMapVector& /*metadata_map_vector*/) {
  // Metadata Frame is not supported in QUIC.
  // TODO(danzh): add stats for metadata not supported error.
}

void EnvoyQuicClientStream::resetStream(Http::StreamResetReason reason) {
  Reset(envoyResetReasonToQuicRstError(reason));
}

void EnvoyQuicClientStream::switchStreamBlockState(bool should_block) {
  ASSERT(FinishedReadingHeaders(),
         "Upper stream buffer limit is reached before response body is delivered.");
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
  ASSERT(headers_decompressed() && !header_list.empty());

  ENVOY_STREAM_LOG(debug, "Received headers: {}.", *this, header_list.DebugString());
  if (fin) {
    end_stream_decoded_ = true;
  }
  std::unique_ptr<Http::ResponseHeaderMapImpl> headers =
      quicHeadersToEnvoyHeaders<Http::ResponseHeaderMapImpl>(header_list);
  const uint64_t status = Http::Utility::getResponseStatus(*headers);
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
    ASSERT(!received_trailers().empty());
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
  quic::QuicSpdyClientStream::OnConnectionClosed(error, source);
  if (!end_stream_decoded_) {
    runResetCallbacks(quicErrorCodeToEnvoyResetReason(error));
  }
}

void EnvoyQuicClientStream::OnClose() {
  quic::QuicSpdyClientStream::OnClose();
  if (BufferedDataBytes() > 0) {
    // If the stream is closed without sending out all buffered data, regard
    // them as sent now and adjust connection buffer book keeping.
    filterManagerConnection()->adjustBytesToSend(0 - BufferedDataBytes());
  }
}

void EnvoyQuicClientStream::OnCanWrite() {
  uint64_t buffered_data_old = BufferedDataBytes();
  quic::QuicSpdyClientStream::OnCanWrite();
  uint64_t buffered_data_new = BufferedDataBytes();
  // As long as OnCanWriteNewData() is no-op, data to sent in buffer shouldn't
  // increase.
  ASSERT(buffered_data_new <= buffered_data_old);
  maybeCheckWatermark(buffered_data_old, buffered_data_new, *filterManagerConnection());
}

uint32_t EnvoyQuicClientStream::streamId() { return id(); }

Network::Connection* EnvoyQuicClientStream::connection() { return filterManagerConnection(); }

QuicFilterManagerConnectionImpl* EnvoyQuicClientStream::filterManagerConnection() {
  return dynamic_cast<QuicFilterManagerConnectionImpl*>(session());
}

} // namespace Quic
} // namespace Envoy
