#include "source/common/quic/envoy_quic_client_stream.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/spdy/core/spdy_header_block.h"
#include "source/common/quic/platform/quic_mem_slice_span_impl.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/envoy_quic_client_session.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/assert.h"

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
  ASSERT(static_cast<uint32_t>(GetReceiveWindow().value()) > 8 * 1024,
         "Send buffer limit should be larger than 8KB.");
}

Http::Status EnvoyQuicClientStream::encodeHeaders(const Http::RequestHeaderMap& headers,
                                                  bool end_stream) {
  // Required headers must be present. This can only happen by some erroneous processing after the
  // downstream codecs decode.
  RETURN_IF_ERROR(Http::HeaderUtility::checkRequiredRequestHeaders(headers));

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
  // Metadata Frame is not supported in QUICHE.
  ENVOY_STREAM_LOG(debug, "METADATA is not supported in Http3.", *this);
  stats_.metadata_not_supported_error_.inc();
}

void EnvoyQuicClientStream::resetStream(Http::StreamResetReason reason) {
  Reset(envoyResetReasonToQuicRstError(reason));
}

void EnvoyQuicClientStream::switchStreamBlockState() {
  // From when the callback got scheduled till now, readDisable() might have blocked and unblocked
  // the stream multiple times, but those actions haven't taken any effect yet, and only the last
  // state of read_disable_counter_ determines whether to unblock or block the quic stream. Unlike
  // Envoy readDisable() the quic stream gets blocked/unblocked based on the most recent call. So a
  // stream will be blocked upon SetBlockedUntilFlush() no matter how many times SetUnblocked() was
  // called before, and vice versa.
  if (read_disable_counter_ > 0) {
    sequencer()->SetBlockedUntilFlush();
  } else {
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
    onStreamError(!http3_options_.override_stream_error_on_invalid_http_message().value(),
                  quic::QUIC_BAD_APPLICATION_PAYLOAD);
    return;
  }

  ENVOY_STREAM_LOG(debug, "Received headers: {}.", *this, header_list.DebugString());
  if (fin) {
    end_stream_decoded_ = true;
  }
  std::unique_ptr<Http::ResponseHeaderMapImpl> headers =
      quicHeadersToEnvoyHeaders<Http::ResponseHeaderMapImpl>(
          header_list, *this, filterManagerConnection()->maxIncomingHeadersCount(), details_);
  if (headers == nullptr) {
    onStreamError(close_connection_upon_invalid_header_, quic::QUIC_STREAM_EXCESSIVE_LOAD);
    return;
  }
  const absl::optional<uint64_t> optional_status =
      Http::Utility::getResponseStatusNoThrow(*headers);
  if (!optional_status.has_value()) {
    details_ = Http3ResponseCodeDetailValues::invalid_http_header;
    onStreamError(!http3_options_.override_stream_error_on_invalid_http_message().value(),
                  quic::QUIC_BAD_APPLICATION_PAYLOAD);
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
    return;
  }

  // Trailers may arrived earlier and wait to be consumed after reading all the body. Consume it
  // here.
  maybeDecodeTrailers();

  OnFinRead();
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
    auto trailers = spdyHeaderBlockToEnvoyTrailers<Http::ResponseTrailerMapImpl>(
        received_trailers(), filterManagerConnection()->maxIncomingHeadersCount(), *this, details_);
    if (trailers == nullptr) {
      onStreamError(close_connection_upon_invalid_header_, quic::QUIC_STREAM_EXCESSIVE_LOAD);
      return;
    }
    response_decoder_->decodeTrailers(std::move(trailers));
    MarkTrailersConsumed();
  }
}

void EnvoyQuicClientStream::OnStreamReset(const quic::QuicRstStreamFrame& frame) {
  ENVOY_STREAM_LOG(debug, "received reset code={}", *this, frame.error_code);
  stats_.rx_reset_.inc();
  quic::QuicSpdyClientStream::OnStreamReset(frame);
  runResetCallbacks(quicRstErrorToEnvoyRemoteResetReason(frame.error_code));
}

void EnvoyQuicClientStream::Reset(quic::QuicRstStreamErrorCode error) {
  ENVOY_STREAM_LOG(debug, "sending reset code={}", *this, error);
  stats_.tx_reset_.inc();
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

void EnvoyQuicClientStream::onStreamError(absl::optional<bool> should_close_connection,
                                          quic::QuicRstStreamErrorCode rst_code) {
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
    Reset(rst_code);
  }
}

} // namespace Quic
} // namespace Envoy
