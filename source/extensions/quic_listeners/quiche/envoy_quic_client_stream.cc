#include "extensions/quic_listeners/quiche/envoy_quic_client_stream.h"

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/spdy/core/spdy_header_block.h"
#include "extensions/quic_listeners/quiche/platform/quic_mem_slice_span_impl.h"

#pragma GCC diagnostic pop

#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"
#include "extensions/quic_listeners/quiche/envoy_quic_client_session.h"

#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"
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

void EnvoyQuicClientStream::encodeHeaders(const Http::RequestHeaderMap& headers, bool end_stream) {
  ENVOY_STREAM_LOG(debug, "encodeHeaders: (end_stream={}) {}.", *this, end_stream, headers);
  WriteHeaders(envoyHeadersToSpdyHeaderBlock(headers), end_stream, nullptr);
  local_end_stream_ = end_stream;
}

void EnvoyQuicClientStream::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_STREAM_LOG(debug, "encodeData (end_stream={}) of {} bytes.", *this, end_stream,
                   data.length());
  local_end_stream_ = end_stream;
  // This is counting not serialized bytes in the send buffer.
  uint64_t bytes_to_send_old = BufferedDataBytes();
  // QUIC stream must take all.
  WriteBodySlices(quic::QuicMemSliceSpan(quic::QuicMemSliceSpanImpl(data)), end_stream);
  if (data.length() > 0) {
    // Send buffer didn't take all the data, threshold needs to be adjusted.
    Reset(quic::QUIC_BAD_APPLICATION_PAYLOAD);
    return;
  }

  uint64_t bytes_to_send_new = BufferedDataBytes();
  ASSERT(bytes_to_send_old <= bytes_to_send_new);
  maybeCheckWatermark(bytes_to_send_old, bytes_to_send_new, *filterManagerConnection());
}

void EnvoyQuicClientStream::encodeTrailers(const Http::RequestTrailerMap& trailers) {
  ASSERT(!local_end_stream_);
  local_end_stream_ = true;
  ENVOY_STREAM_LOG(debug, "encodeTrailers: {}.", *this, trailers);
  WriteTrailers(envoyHeadersToSpdyHeaderBlock(trailers), nullptr);
}

void EnvoyQuicClientStream::encodeMetadata(const Http::MetadataMapVector& /*metadata_map_vector*/) {
  // Metadata Frame is not supported in QUIC.
  // TODO(danzh): add stats for metadata not supported error.
}

void EnvoyQuicClientStream::resetStream(Http::StreamResetReason reason) {
  // Higher layers expect calling resetStream() to immediately raise reset callbacks.
  runResetCallbacks(reason);
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
  quic::QuicSpdyStream::OnInitialHeadersComplete(fin, frame_len, header_list);
  if (rst_sent()) {
    return;
  }
  ASSERT(headers_decompressed());
  response_decoder_->decodeHeaders(
      quicHeadersToEnvoyHeaders<Http::ResponseHeaderMapImpl>(header_list), /*end_stream=*/fin);
  if (fin) {
    end_stream_decoded_ = true;
  }
  ConsumeHeaderList();
}

void EnvoyQuicClientStream::OnBodyAvailable() {
  ASSERT(FinishedReadingHeaders());
  ASSERT(read_disable_counter_ == 0);
  ASSERT(!in_decode_data_callstack_);
  in_decode_data_callstack_ = true;

  Buffer::InstancePtr buffer = std::make_unique<Buffer::OwnedImpl>();
  // TODO(danzh): check Envoy per stream buffer limit.
  // Currently read out all the data.
  while (HasBytesToRead()) {
    iovec iov;
    int num_regions = GetReadableRegions(&iov, 1);
    ASSERT(num_regions > 0);
    size_t bytes_read = iov.iov_len;
    Buffer::RawSlice slice;
    buffer->reserve(bytes_read, &slice, 1);
    ASSERT(slice.len_ >= bytes_read);
    slice.len_ = bytes_read;
    memcpy(slice.mem_, iov.iov_base, iov.iov_len);
    buffer->commit(&slice, 1);
    MarkConsumed(bytes_read);
  }

  // True if no trailer and FIN read.
  bool finished_reading = IsDoneReading();
  bool empty_payload_with_fin = buffer->length() == 0 && fin_received();
  // If this call is triggered by an empty frame with FIN which is not from peer
  // but synthesized by stream itself upon receiving HEADERS with FIN or
  // TRAILERS, do not deliver end of stream here. Because either decodeHeaders
  // already delivered it or decodeTrailers will be called.
  bool skip_decoding = empty_payload_with_fin && (end_stream_decoded_ || !finished_reading);
  if (!skip_decoding) {
    response_decoder_->decodeData(*buffer, finished_reading);
    if (finished_reading) {
      end_stream_decoded_ = true;
    }
  }

  if (!sequencer()->IsClosed()) {
    in_decode_data_callstack_ = false;
    if (read_disable_counter_ > 0) {
      // If readDisable() was ever called during decodeData() and it meant to disable
      // reading from downstream, the call must have been deferred. Call it now.
      switchStreamBlockState(true);
    }
    return;
  }

  if (!quic::VersionUsesHttp3(transport_version()) && !FinishedReadingTrailers()) {
    // For Google QUIC implementation, trailers may arrived earlier and wait to
    // be consumed after reading all the body. Consume it here.
    // IETF QUIC shouldn't reach here because trailers are sent on same stream.
    response_decoder_->decodeTrailers(
        spdyHeaderBlockToEnvoyHeaders<Http::ResponseTrailerMapImpl>(received_trailers()));
    MarkTrailersConsumed();
  }
  OnFinRead();
  in_decode_data_callstack_ = false;
}

void EnvoyQuicClientStream::OnTrailingHeadersComplete(bool fin, size_t frame_len,
                                                      const quic::QuicHeaderList& header_list) {
  quic::QuicSpdyStream::OnTrailingHeadersComplete(fin, frame_len, header_list);
  ASSERT(trailers_decompressed());
  if (session()->connection()->connected() &&
      (quic::VersionUsesHttp3(transport_version()) || sequencer()->IsClosed()) &&
      !FinishedReadingTrailers()) {
    // Before QPack, trailers can arrive before body. Only decode trailers after finishing decoding
    // body.
    response_decoder_->decodeTrailers(
        spdyHeaderBlockToEnvoyHeaders<Http::ResponseTrailerMapImpl>(received_trailers()));
    MarkTrailersConsumed();
  }
}

void EnvoyQuicClientStream::OnStreamReset(const quic::QuicRstStreamFrame& frame) {
  quic::QuicSpdyClientStream::OnStreamReset(frame);
  runResetCallbacks(quicRstErrorToEnvoyResetReason(frame.error_code));
}

void EnvoyQuicClientStream::OnConnectionClosed(quic::QuicErrorCode error,
                                               quic::ConnectionCloseSource source) {
  quic::QuicSpdyClientStream::OnConnectionClosed(error, source);
  runResetCallbacks(quicErrorCodeToEnvoyResetReason(error));
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
