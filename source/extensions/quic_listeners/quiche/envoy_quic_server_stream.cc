#include "extensions/quic_listeners/quiche/envoy_quic_server_stream.h"

#include <openssl/bio.h>
#include <openssl/evp.h>

#include <memory>

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/spdy/core/spdy_header_block.h"
#include "extensions/quic_listeners/quiche/platform/quic_mem_slice_span_impl.h"
#pragma GCC diagnostic pop

#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"
#include "extensions/quic_listeners/quiche/envoy_quic_server_session.h"

#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"
#include "common/common/assert.h"

namespace Envoy {
namespace Quic {

EnvoyQuicServerStream::EnvoyQuicServerStream(quic::QuicStreamId id, quic::QuicSpdySession* session,
                                             quic::StreamType type)
    : quic::QuicSpdyServerStreamBase(id, session, type),
      EnvoyQuicStream(
          session->config()->GetInitialStreamFlowControlWindowToSend(),
          [this]() { runLowWatermarkCallbacks(); }, [this]() { runHighWatermarkCallbacks(); }) {}

EnvoyQuicServerStream::EnvoyQuicServerStream(quic::PendingStream* pending,
                                             quic::QuicSpdySession* session, quic::StreamType type)
    : quic::QuicSpdyServerStreamBase(pending, session, type),
      EnvoyQuicStream(
          session->config()->GetInitialStreamFlowControlWindowToSend(),
          [this]() { runLowWatermarkCallbacks(); }, [this]() { runHighWatermarkCallbacks(); }) {}

void EnvoyQuicServerStream::encode100ContinueHeaders(const Http::HeaderMap& headers) {
  ASSERT(headers.Status()->value() == "100");
  encodeHeaders(headers, false);
}

void EnvoyQuicServerStream::encodeHeaders(const Http::HeaderMap& headers, bool end_stream) {
  ENVOY_STREAM_LOG(debug, "encodeHeaders (end_stream={}) {}.", *this, end_stream, headers);
  WriteHeaders(envoyHeadersToSpdyHeaderBlock(headers), end_stream, nullptr);
  local_end_stream_ = end_stream;
}

void EnvoyQuicServerStream::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_STREAM_LOG(debug, "encodeData (end_stream={}) of {} bytes.", *this, end_stream,
                   data.length());
  local_end_stream_ = end_stream;
  // This is counting not serialized bytes in the send buffer.
  uint64_t bytes_to_send_old = BufferedDataBytes();
  // QUIC stream must take all.
  WriteBodySlices(quic::QuicMemSliceSpan(quic::QuicMemSliceSpanImpl(data)), end_stream);
  ASSERT(data.length() == 0);

  uint64_t bytes_to_send_new = BufferedDataBytes();
  ASSERT(bytes_to_send_old <= bytes_to_send_new);
  if (bytes_to_send_new > bytes_to_send_old) {
    // If buffered bytes changed, update stream and session's watermark book
    // keeping.
    sendBufferSimulation().checkHighWatermark(bytes_to_send_new);
    dynamic_cast<EnvoyQuicServerSession*>(session())->adjustBytesToSend(bytes_to_send_new -
                                                                        bytes_to_send_old);
  }
}

void EnvoyQuicServerStream::encodeTrailers(const Http::HeaderMap& trailers) {
  ASSERT(!local_end_stream_);
  local_end_stream_ = true;
  ENVOY_STREAM_LOG(debug, "encodeTrailers: {}.", *this, trailers);
  WriteTrailers(envoyHeadersToSpdyHeaderBlock(trailers), nullptr);
}

void EnvoyQuicServerStream::encodeMetadata(const Http::MetadataMapVector& /*metadata_map_vector*/) {
  // Metadata Frame is not supported in QUIC.
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

void EnvoyQuicServerStream::resetStream(Http::StreamResetReason reason) {
  if (local_end_stream_ && !reading_stopped()) {
    // This is after 200 early response. Reset with QUIC_STREAM_NO_ERROR instead
    // of propagating original reset reason.
    StopReading();
  } else {
    Reset(envoyResetReasonToQuicRstError(reason));
  }
}

void EnvoyQuicServerStream::switchStreamBlockState(bool should_block) {
  ASSERT(FinishedReadingHeaders(),
         "Upperstream buffer limit is reached before request body is delivered.");
  if (should_block) {
    sequencer()->SetBlockedUntilFlush();
  } else {
    ASSERT(read_disable_counter_ == 0, "readDisable called in btw.");
    sequencer()->SetUnblocked();
  }
}

void EnvoyQuicServerStream::OnInitialHeadersComplete(bool fin, size_t frame_len,
                                                     const quic::QuicHeaderList& header_list) {
  quic::QuicSpdyServerStreamBase::OnInitialHeadersComplete(fin, frame_len, header_list);
  ASSERT(decoder() != nullptr);
  ASSERT(headers_decompressed());
  decoder()->decodeHeaders(quicHeadersToEnvoyHeaders(header_list), /*end_stream=*/fin);
  if (fin) {
    end_stream_decoded_ = true;
  }
  ConsumeHeaderList();
}

void EnvoyQuicServerStream::OnBodyAvailable() {
  ASSERT(FinishedReadingHeaders());
  ASSERT(read_disable_counter_ == 0);
  ASSERT(!in_encode_data_callstack_);
  in_encode_data_callstack_ = true;

  Buffer::InstancePtr buffer = std::make_unique<Buffer::OwnedImpl>();
  // TODO(danzh): check Envoy per stream buffer limit.
  // Currently read out all the data.
  while (HasBytesToRead()) {
    struct iovec iov;
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
  bool empty_payload_with_fin = buffer->length() == 0 && finished_reading;
  if (!empty_payload_with_fin || !end_stream_decoded_) {
    ASSERT(decoder() != nullptr);
    decoder()->decodeData(*buffer, finished_reading);
    if (finished_reading) {
      end_stream_decoded_ = true;
    }
  }

  if (!sequencer()->IsClosed()) {
    in_encode_data_callstack_ = false;
    if (read_disable_counter_ > 0) {
      // If readDisable() was ever called during decodeData() and it meant to disable
      // reading from downstream, the call must have been deferred. Call it now.
      switchStreamBlockState(true);
    }
    return;
  }

  if (!quic::VersionUsesQpack(transport_version()) && !FinishedReadingTrailers()) {
    // For Google QUIC implementation, trailers may arrived earlier and wait to
    // be consumed after reading all the body. Consume it here.
    // IETF QUIC shouldn't reach here because trailers are sent on same stream.
    decoder()->decodeTrailers(spdyHeaderBlockToEnvoyHeaders(received_trailers()));
    MarkTrailersConsumed();
  }
  OnFinRead();
  in_encode_data_callstack_ = false;
}

void EnvoyQuicServerStream::OnTrailingHeadersComplete(bool fin, size_t frame_len,
                                                      const quic::QuicHeaderList& header_list) {
  quic::QuicSpdyServerStreamBase::OnTrailingHeadersComplete(fin, frame_len, header_list);
  if (session()->connection()->connected() &&
      (quic::VersionUsesQpack(transport_version()) || sequencer()->IsClosed()) &&
      !FinishedReadingTrailers()) {
    // Before QPack trailers can arrive before body. Only decode trailers after finishing decoding
    // body.
    ASSERT(decoder() != nullptr);
    decoder()->decodeTrailers(spdyHeaderBlockToEnvoyHeaders(received_trailers()));
    MarkTrailersConsumed();
  }
}

void EnvoyQuicServerStream::OnStreamReset(const quic::QuicRstStreamFrame& frame) {
  quic::QuicSpdyServerStreamBase::OnStreamReset(frame);
  runResetCallbacks(quicRstErrorToEnvoyResetReason(frame.error_code));
}

void EnvoyQuicServerStream::OnConnectionClosed(quic::QuicErrorCode error,
                                               quic::ConnectionCloseSource source) {
  quic::QuicSpdyServerStreamBase::OnConnectionClosed(error, source);
  runResetCallbacks(quicErrorCodeToEnvoyResetReason(error));
}

void EnvoyQuicServerStream::OnCanWrite() {
  uint64_t buffered_data_old = BufferedDataBytes();
  quic::QuicSpdyServerStreamBase::OnCanWrite();
  uint64_t buffered_data_new = BufferedDataBytes();
  // As long as OnCanWriteNewData() is no-op, data to sent in buffer shouldn't
  // increase.
  ASSERT(buffered_data_new <= buffered_data_old);
  if (buffered_data_new < buffered_data_old) {
    sendBufferSimulation().checkLowWatermark(buffered_data_new);
    dynamic_cast<EnvoyQuicServerSession*>(session())->adjustBytesToSend(buffered_data_new -
                                                                        buffered_data_old);
  }
}

uint32_t EnvoyQuicServerStream::streamId() { return id(); }

Network::Connection* EnvoyQuicServerStream::connection() {
  return dynamic_cast<EnvoyQuicServerSession*>(session());
}

} // namespace Quic
} // namespace Envoy
