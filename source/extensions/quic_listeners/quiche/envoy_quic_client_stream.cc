#include "extensions/quic_listeners/quiche/envoy_quic_client_stream.h"

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/quic_session.h"

#pragma GCC diagnostic pop

#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"

namespace Envoy {
namespace Quic {

EnvoyQuicClientStream::EnvoyQuicClientStream(quic::QuicStreamId id,
                                             quic::QuicSpdyClientSession* client_session,
                                             quic::StreamType type)
    : quic::QuicSpdyClientStream(id, client_session, type),
      EnvoyQuicStream(
          session()->config()->GetInitialStreamFlowControlWindowToSend(),
          [this]() { runLowWatermarkCallbacks(); }, [this]() { runHighWatermarkCallbacks(); }) {}

EnvoyQuicClientStream::EnvoyQuicClientStream(quic::PendingStream* pending,
                                             quic::QuicSpdyClientSession* client_session,
                                             quic::StreamType type)
    : quic::QuicSpdyClientStream(pending, client_session, type),
      EnvoyQuicStream(
          session()->config()->GetInitialStreamFlowControlWindowToSend(),
          [this]() { runLowWatermarkCallbacks(); }, [this]() { runHighWatermarkCallbacks(); }) {}

void EnvoyQuicClientStream::encode100ContinueHeaders(const Http::HeaderMap& headers) {
  ASSERT(headers.Status()->value() == "100");
  encodeHeaders(headers, false);
}

void EnvoyQuicClientStream::encodeHeaders(const Http::HeaderMap& /*headers*/, bool /*end_stream*/) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}
void EnvoyQuicClientStream::encodeData(Buffer::Instance& /*data*/, bool /*end_stream*/) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}
void EnvoyQuicClientStream::encodeTrailers(const Http::HeaderMap& /*trailers*/) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}
void EnvoyQuicClientStream::encodeMetadata(const Http::MetadataMapVector& /*metadata_map_vector*/) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

void EnvoyQuicClientStream::resetStream(Http::StreamResetReason /*reason*/) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

void EnvoyQuicClientStream::readDisable(bool /*disable*/) { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

void EnvoyQuicClientStream::OnInitialHeadersComplete(bool fin, size_t frame_len,
                                                     const quic::QuicHeaderList& header_list) {
  quic::QuicSpdyClientStream::OnInitialHeadersComplete(fin, frame_len, header_list);
  if (!rst_sent()) {
    ASSERT(FinishedReadingHeaders());
    ASSERT(decoder() != nullptr);
    decoder()->decodeHeaders(spdyHeaderBlockToEnvoyHeaders(response_headers()), /*end_stream=*/fin);
  }
}

void EnvoyQuicClientStream::OnBodyAvailable() {
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
  // If this is the last stream data, set end_stream if there is no
  // trailers.
  ASSERT(decoder() != nullptr);
  decoder()->decodeData(*buffer, finished_reading);

  if (!sequencer()->IsClosed()) {
    sequencer()->SetUnblocked();
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
}

void EnvoyQuicClientStream::OnTrailingHeadersComplete(bool fin, size_t frame_len,
                                                      const quic::QuicHeaderList& header_list) {
  quic::QuicSpdyStream::OnTrailingHeadersComplete(fin, frame_len, header_list);
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

void EnvoyQuicClientStream::OnStreamReset(const quic::QuicRstStreamFrame& frame) {
  quic::QuicSpdyClientStream::OnStreamReset(frame);
  Http::StreamResetReason reason;
  if (frame.error_code == quic::QUIC_REFUSED_STREAM) {
    reason = Http::StreamResetReason::RemoteRefusedStreamReset;
  } else {
    reason = Http::StreamResetReason::RemoteReset;
  }
  runResetCallbacks(reason);
}

void EnvoyQuicClientStream::OnConnectionClosed(quic::QuicErrorCode error,
                                               quic::ConnectionCloseSource source) {
  quic::QuicSpdyClientStream::OnConnectionClosed(error, source);
  Http::StreamResetReason reason;
  if (error == quic::QUIC_NO_ERROR) {
    reason = Http::StreamResetReason::ConnectionTermination;
  } else {
    reason = Http::StreamResetReason::ConnectionFailure;
  }
  runResetCallbacks(reason);
}

} // namespace Quic
} // namespace Envoy
