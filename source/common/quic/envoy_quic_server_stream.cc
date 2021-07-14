#include "source/common/quic/envoy_quic_server_stream.h"

#include <openssl/bio.h>
#include <openssl/evp.h>

#include <memory>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/spdy/core/spdy_header_block.h"
#include "source/common/quic/platform/quic_mem_slice_span_impl.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/envoy_quic_server_session.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/common/assert.h"
#include "source/common/http/header_utility.h"

namespace Envoy {
namespace Quic {

EnvoyQuicServerStream::EnvoyQuicServerStream(
    quic::QuicStreamId id, quic::QuicSpdySession* session, quic::StreamType type,
    Http::Http3::CodecStats& stats,
    const envoy::config::core::v3::Http3ProtocolOptions& http3_options,
    envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
        headers_with_underscores_action)
    : quic::QuicSpdyServerStreamBase(id, session, type),
      EnvoyQuicStream(
          // Flow control receive window should be larger than 8k to fully utilize congestion
          // control window before it reaches the high watermark.
          static_cast<uint32_t>(GetReceiveWindow().value()), *filterManagerConnection(),
          [this]() { runLowWatermarkCallbacks(); }, [this]() { runHighWatermarkCallbacks(); },
          stats, http3_options),
      headers_with_underscores_action_(headers_with_underscores_action) {
  ASSERT(static_cast<uint32_t>(GetReceiveWindow().value()) > 8 * 1024,
         "Send buffer limit should be larger than 8KB.");
}

EnvoyQuicServerStream::EnvoyQuicServerStream(
    quic::PendingStream* pending, quic::QuicSpdySession* session, quic::StreamType type,
    Http::Http3::CodecStats& stats,
    const envoy::config::core::v3::Http3ProtocolOptions& http3_options,
    envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
        headers_with_underscores_action)
    : quic::QuicSpdyServerStreamBase(pending, session, type),
      EnvoyQuicStream(
          // This should be larger than 8k to fully utilize congestion control
          // window. And no larger than the max stream flow control window for
          // the stream to buffer all the data.
          static_cast<uint32_t>(GetReceiveWindow().value()), *filterManagerConnection(),
          [this]() { runLowWatermarkCallbacks(); }, [this]() { runHighWatermarkCallbacks(); },
          stats, http3_options),
      headers_with_underscores_action_(headers_with_underscores_action) {}

void EnvoyQuicServerStream::encode100ContinueHeaders(const Http::ResponseHeaderMap& headers) {
  ASSERT(headers.Status()->value() == "100");
  encodeHeaders(headers, false);
}

void EnvoyQuicServerStream::encodeHeaders(const Http::ResponseHeaderMap& headers, bool end_stream) {
  ENVOY_STREAM_LOG(debug, "encodeHeaders (end_stream={}) {}.", *this, end_stream, headers);
  // QUICHE guarantees to take all the headers. This could cause infinite data to
  // be buffered on headers stream in Google QUIC implementation because
  // headers stream doesn't have upper bound for its send buffer. But in IETF
  // QUIC implementation this is safe as headers are sent on data stream which
  // is bounded by max concurrent streams limited.
  // Same vulnerability exists in crypto stream which can infinitely buffer data
  // if handshake implementation goes wrong.
  // TODO(#8826) Modify QUICHE to have an upper bound for header stream send buffer.
  // This is counting not serialized bytes in the send buffer.
  local_end_stream_ = end_stream;
  SendBufferMonitor::ScopedWatermarkBufferUpdater updater(this, this);
  WriteHeaders(envoyHeadersToSpdyHeaderBlock(headers), end_stream, nullptr);
}

void EnvoyQuicServerStream::encodeData(Buffer::Instance& data, bool end_stream) {
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

void EnvoyQuicServerStream::encodeTrailers(const Http::ResponseTrailerMap& trailers) {
  ASSERT(!local_end_stream_);
  local_end_stream_ = true;
  ENVOY_STREAM_LOG(debug, "encodeTrailers: {}.", *this, trailers);
  SendBufferMonitor::ScopedWatermarkBufferUpdater updater(this, this);
  WriteTrailers(envoyHeadersToSpdyHeaderBlock(trailers), nullptr);
}

void EnvoyQuicServerStream::encodeMetadata(const Http::MetadataMapVector& /*metadata_map_vector*/) {
  // Metadata Frame is not supported in QUIC.
  ENVOY_STREAM_LOG(debug, "METADATA is not supported in Http3.", *this);
  stats_.metadata_not_supported_error_.inc();
}

void EnvoyQuicServerStream::resetStream(Http::StreamResetReason reason) {
  if (local_end_stream_ && !reading_stopped()) {
    // This is after 200 early response. Reset with QUIC_STREAM_NO_ERROR instead
    // of propagating original reset reason. In QUICHE if a stream stops reading
    // before FIN or RESET received, it resets the steam with QUIC_STREAM_NO_ERROR.
    StopReading();
    runResetCallbacks(Http::StreamResetReason::LocalReset);
  } else {
    Reset(envoyResetReasonToQuicRstError(reason));
  }
}

void EnvoyQuicServerStream::switchStreamBlockState() {
  // From when the callback got scheduled till now, readDisable() might have blocked and unblocked
  // the stream multiple times, but those actions haven't taken any effect yet, and only the last
  // state of read_disable_counter_ determines whether to unblock or block the quic stream.
  // Unlike Envoy readDisable() the quic stream gets blocked/unblocked based on the most recent
  // call. So a stream will be blocked upon SetBlockedUntilFlush() no matter how many times
  // SetUnblocked() was called before, and vice versa.
  if (read_disable_counter_ > 0) {
    sequencer()->SetBlockedUntilFlush();
  } else {
    sequencer()->SetUnblocked();
  }
}

void EnvoyQuicServerStream::OnInitialHeadersComplete(bool fin, size_t frame_len,
                                                     const quic::QuicHeaderList& header_list) {
  // TODO(danzh) Fix in QUICHE. If the stream has been reset in the call stack,
  // OnInitialHeadersComplete() shouldn't be called.
  if (read_side_closed()) {
    return;
  }
  quic::QuicSpdyServerStreamBase::OnInitialHeadersComplete(fin, frame_len, header_list);
  if (!headers_decompressed() || header_list.empty()) {
    onStreamError(absl::nullopt);
    return;
  }

  ENVOY_STREAM_LOG(debug, "Received headers: {}.", *this, header_list.DebugString());
  if (fin) {
    end_stream_decoded_ = true;
  }
  std::unique_ptr<Http::RequestHeaderMapImpl> headers =
      quicHeadersToEnvoyHeaders<Http::RequestHeaderMapImpl>(
          header_list, *this, filterManagerConnection()->maxIncomingHeadersCount(), details_);
  if (headers == nullptr) {
    onStreamError(close_connection_upon_invalid_header_);
    return;
  }
  if (Http::HeaderUtility::requestHeadersValid(*headers) != absl::nullopt) {
    details_ = Http3ResponseCodeDetailValues::invalid_http_header;
    onStreamError(absl::nullopt);
    return;
  }
  request_decoder_->decodeHeaders(std::move(headers),
                                  /*end_stream=*/fin);
  ConsumeHeaderList();
}

void EnvoyQuicServerStream::OnBodyAvailable() {
  ASSERT(FinishedReadingHeaders());
  if (read_side_closed()) {
    return;
  }

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

  bool fin_read_and_no_trailers = IsDoneReading();
  ENVOY_STREAM_LOG(debug, "Received {} bytes of data {} FIN.", *this, buffer->length(),
                   fin_read_and_no_trailers ? "with" : "without");
  // If this call is triggered by an empty frame with FIN which is not from peer
  // but synthesized by stream itself upon receiving HEADERS with FIN or
  // TRAILERS, do not deliver end of stream here. Because either decodeHeaders
  // already delivered it or decodeTrailers will be called.
  bool skip_decoding = (buffer->length() == 0 && !fin_read_and_no_trailers) || end_stream_decoded_;
  if (!skip_decoding) {
    if (fin_read_and_no_trailers) {
      end_stream_decoded_ = true;
    }
    request_decoder_->decodeData(*buffer, fin_read_and_no_trailers);
  }

  if (!sequencer()->IsClosed() || read_side_closed()) {
    return;
  }

  // Trailers may arrived earlier and wait to be consumed after reading all the body. Consume it
  // here.
  maybeDecodeTrailers();

  OnFinRead();
}

void EnvoyQuicServerStream::OnTrailingHeadersComplete(bool fin, size_t frame_len,
                                                      const quic::QuicHeaderList& header_list) {
  if (read_side_closed()) {
    return;
  }
  ENVOY_STREAM_LOG(debug, "Received trailers: {}.", *this, received_trailers().DebugString());
  quic::QuicSpdyServerStreamBase::OnTrailingHeadersComplete(fin, frame_len, header_list);
  ASSERT(trailers_decompressed());
  if (session()->connection()->connected() && !rst_sent()) {
    maybeDecodeTrailers();
  }
}

void EnvoyQuicServerStream::OnHeadersTooLarge() {
  ENVOY_STREAM_LOG(debug, "Headers too large.", *this);
  details_ = Http3ResponseCodeDetailValues::headers_too_large;
  quic::QuicSpdyServerStreamBase::OnHeadersTooLarge();
}

void EnvoyQuicServerStream::maybeDecodeTrailers() {
  if (sequencer()->IsClosed() && !FinishedReadingTrailers()) {
    // Only decode trailers after finishing decoding body.
    end_stream_decoded_ = true;
    if (received_trailers().size() > filterManagerConnection()->maxIncomingHeadersCount()) {
      details_ = Http3ResponseCodeDetailValues::too_many_trailers;
      onStreamError(close_connection_upon_invalid_header_);
      return;
    }
    request_decoder_->decodeTrailers(
        spdyHeaderBlockToEnvoyHeaders<Http::RequestTrailerMapImpl>(received_trailers()));
    MarkTrailersConsumed();
  }
}

bool EnvoyQuicServerStream::OnStopSending(quic::QuicRstStreamErrorCode error) {
  // Only called in IETF Quic to close write side.
  ENVOY_STREAM_LOG(debug, "received STOP_SENDING with reset code={}", *this, error);
  stats_.rx_reset_.inc();
  bool end_stream_encoded = local_end_stream_;
  // This call will close write.
  bool ret = quic::QuicSpdyServerStreamBase::OnStopSending(error);
  ASSERT(write_side_closed());
  if (read_side_closed() && !end_stream_encoded) {
    // If both directions are closed but end stream hasn't been encoded yet, notify reset callbacks.
    // Treat this as a remote reset, since the stream will be closed in both directions.
    runResetCallbacks(quicRstErrorToEnvoyRemoteResetReason(error));
  }
  return ret;
}

void EnvoyQuicServerStream::OnStreamReset(const quic::QuicRstStreamFrame& frame) {
  ENVOY_STREAM_LOG(debug, "received RESET_STREAM with reset code={}", *this, frame.error_code);
  stats_.rx_reset_.inc();
  bool end_stream_decoded_and_encoded = read_side_closed() && local_end_stream_;
  // This closes read side in both Google Quic and IETF Quic, but doesn't close write side in IETF
  // Quic.
  quic::QuicSpdyServerStreamBase::OnStreamReset(frame);
  ASSERT(read_side_closed());
  if (write_side_closed() && !end_stream_decoded_and_encoded) {
    // If both directions are closed but upstream hasn't received or sent end stream, run reset
    // stream callback.
    runResetCallbacks(quicRstErrorToEnvoyRemoteResetReason(frame.error_code));
  }
}

void EnvoyQuicServerStream::Reset(quic::QuicRstStreamErrorCode error) {
  ENVOY_STREAM_LOG(debug, "sending reset code={}", *this, error);
  stats_.tx_reset_.inc();
  // Upper layers expect calling resetStream() to immediately raise reset callbacks.
  runResetCallbacks(quicRstErrorToEnvoyLocalResetReason(error));
  quic::QuicSpdyServerStreamBase::Reset(error);
}

void EnvoyQuicServerStream::OnConnectionClosed(quic::QuicErrorCode error,
                                               quic::ConnectionCloseSource source) {
  // Run reset callback before closing the stream so that the watermark change will not trigger
  // callbacks.
  if (!local_end_stream_) {
    runResetCallbacks(source == quic::ConnectionCloseSource::FROM_SELF
                          ? quicErrorCodeToEnvoyLocalResetReason(error)
                          : quicErrorCodeToEnvoyRemoteResetReason(error));
  }
  quic::QuicSpdyServerStreamBase::OnConnectionClosed(error, source);
}

void EnvoyQuicServerStream::OnClose() {
  quic::QuicSpdyServerStreamBase::OnClose();
  if (isDoingWatermarkAccounting()) {
    return;
  }
  clearWatermarkBuffer();
}

void EnvoyQuicServerStream::clearWatermarkBuffer() {
  if (BufferedDataBytes() > 0) {
    // If the stream is closed without sending out all buffered data, regard
    // them as sent now and adjust connection buffer book keeping.
    updateBytesBuffered(BufferedDataBytes(), 0);
  }
}

void EnvoyQuicServerStream::OnCanWrite() {
  SendBufferMonitor::ScopedWatermarkBufferUpdater updater(this, this);
  quic::QuicSpdyServerStreamBase::OnCanWrite();
}

uint32_t EnvoyQuicServerStream::streamId() { return id(); }

Network::Connection* EnvoyQuicServerStream::connection() { return filterManagerConnection(); }

QuicFilterManagerConnectionImpl* EnvoyQuicServerStream::filterManagerConnection() {
  return dynamic_cast<QuicFilterManagerConnectionImpl*>(session());
}

Http::HeaderUtility::HeaderValidationResult
EnvoyQuicServerStream::validateHeader(const std::string& header_name,
                                      absl::string_view header_value) {
  Http::HeaderUtility::HeaderValidationResult result =
      EnvoyQuicStream::validateHeader(header_name, header_value);
  if (result != Http::HeaderUtility::HeaderValidationResult::ACCEPT) {
    return result;
  }
  // Do request specific checks.
  result = Http::HeaderUtility::checkHeaderNameForUnderscores(
      header_name, headers_with_underscores_action_, stats_.dropped_headers_with_underscores_,
      stats_.requests_rejected_with_underscores_in_headers_);
  if (result != Http::HeaderUtility::HeaderValidationResult::ACCEPT) {
    details_ = Http3ResponseCodeDetailValues::invalid_underscore;
  }
  return result;
}

void EnvoyQuicServerStream::onStreamError(absl::optional<bool> should_close_connection) {
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
