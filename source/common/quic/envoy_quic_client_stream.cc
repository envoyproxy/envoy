#include "source/common/quic/envoy_quic_client_stream.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"
#include "source/common/quic/envoy_quic_client_session.h"
#include "source/common/quic/envoy_quic_utils.h"

#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/spdy/core/http2_header_block.h"

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
  ENVOY_STREAM_LOG(debug, "encodeHeaders: (end_stream={}) {}.", *this, end_stream, headers);
  // Required headers must be present. This can only happen by some erroneous processing after the
  // downstream codecs decode.
  RETURN_IF_ERROR(Http::HeaderUtility::checkRequiredRequestHeaders(headers));
  if (write_side_closed()) {
    return absl::CancelledError("encodeHeaders is called on write-closed stream.");
  }

  local_end_stream_ = end_stream;
  SendBufferMonitor::ScopedWatermarkBufferUpdater updater(this, this);
  auto spdy_headers = envoyHeadersToHttp2HeaderBlock(headers);
  if (headers.Method()) {
    if (headers.Method()->value() == "CONNECT") {
      if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.use_rfc_connect")) {
        spdy_headers.erase(":scheme");
        spdy_headers.erase(":path");
        spdy_headers.erase(":protocol");
      } else {
        // Legacy support for abandoned
        // https://tools.ietf.org/html/draft-kinnear-httpbis-http2-transport-02
        spdy_headers[":protocol"] = Http::Headers::get().ProtocolValues.Bytestream;
        if (!headers.Path()) {
          spdy_headers[":path"] = "/";
        }
      }
    } else if (headers.Method()->value() == "HEAD") {
      sent_head_request_ = true;
    }
  }
  {
    IncrementalBytesSentTracker tracker(*this, *mutableBytesMeter(), true);
    size_t bytes_sent = WriteHeaders(std::move(spdy_headers), end_stream, nullptr);
    ENVOY_BUG(bytes_sent != 0, "Failed to encode headers.");
  }

  if (local_end_stream_) {
    onLocalEndStream();
  }
  return Http::okStatus();
}

void EnvoyQuicClientStream::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_STREAM_LOG(debug, "encodeData (end_stream={}) of {} bytes.", *this, end_stream,
                   data.length());
  const bool has_data = data.length() > 0;
  if (!has_data && !end_stream) {
    return;
  }
  if (write_side_closed()) {
    IS_ENVOY_BUG("encodeData is called on write-closed stream.");
    return;
  }
  ASSERT(!local_end_stream_);
  local_end_stream_ = end_stream;
  SendBufferMonitor::ScopedWatermarkBufferUpdater updater(this, this);
  Buffer::RawSliceVector raw_slices = data.getRawSlices();
  absl::InlinedVector<quiche::QuicheMemSlice, 4> quic_slices;
  quic_slices.reserve(raw_slices.size());
  for (auto& slice : raw_slices) {
    ASSERT(slice.len_ != 0);
    // Move each slice into a stand-alone buffer.
    // TODO(danzh): investigate the cost of allocating one buffer per slice.
    // If it turns out to be expensive, add a new function to free data in the middle in buffer
    // interface and re-design QuicheMemSliceImpl.
    quic_slices.emplace_back(quiche::QuicheMemSlice::InPlace(), data, slice.len_);
  }
  quic::QuicConsumedData result{0, false};
  absl::Span<quiche::QuicheMemSlice> span(quic_slices);
  {
    IncrementalBytesSentTracker tracker(*this, *mutableBytesMeter(), false);
    result = WriteBodySlices(span, end_stream);
  }
  // QUIC stream must take all.
  if (result.bytes_consumed == 0 && has_data) {
    IS_ENVOY_BUG(fmt::format("Send buffer didn't take all the data. Stream is write {} with {} "
                             "bytes in send buffer. Current write was rejected.",
                             write_side_closed() ? "closed" : "open", BufferedDataBytes()));
    Reset(quic::QUIC_BAD_APPLICATION_PAYLOAD);
    return;
  }
  if (local_end_stream_) {
    onLocalEndStream();
  }
}

void EnvoyQuicClientStream::encodeTrailers(const Http::RequestTrailerMap& trailers) {
  ENVOY_STREAM_LOG(debug, "encodeTrailers: {}.", *this, trailers);
  if (write_side_closed()) {
    IS_ENVOY_BUG("encodeTrailers is called on write-closed stream.");
    return;
  }
  ASSERT(!local_end_stream_);
  local_end_stream_ = true;
  ScopedWatermarkBufferUpdater updater(this, this);

  {
    IncrementalBytesSentTracker tracker(*this, *mutableBytesMeter(), true);
    size_t bytes_sent = WriteTrailers(envoyHeadersToHttp2HeaderBlock(trailers), nullptr);
    ENVOY_BUG(bytes_sent != 0, "Failed to encode trailers");
  }

  onLocalEndStream();
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
  mutableBytesMeter()->addHeaderBytesReceived(frame_len);
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

  quic::QuicRstStreamErrorCode transform_rst = quic::QUIC_STREAM_NO_ERROR;
  std::unique_ptr<Http::ResponseHeaderMapImpl> headers =
      quicHeadersToEnvoyHeaders<Http::ResponseHeaderMapImpl>(
          header_list, *this, filterManagerConnection()->maxIncomingHeadersCount(), details_,
          transform_rst);
  if (headers == nullptr) {
    onStreamError(close_connection_upon_invalid_header_, transform_rst);
    return;
  }
  const absl::optional<uint64_t> optional_status =
      Http::Utility::getResponseStatusOrNullopt(*headers);
  if (!optional_status.has_value()) {
    details_ = Http3ResponseCodeDetailValues::invalid_http_header;
    onStreamError(!http3_options_.override_stream_error_on_invalid_http_message().value(),
                  quic::QUIC_BAD_APPLICATION_PAYLOAD);
    return;
  }
  const uint64_t status = optional_status.value();
  if (Http::CodeUtility::is1xx(status)) {
    // These are Informational 1xx headers, not the actual response headers.
    set_headers_decompressed(false);
  }

  const bool is_special_1xx = Http::HeaderUtility::isSpecial1xx(*headers);
  if (is_special_1xx && !decoded_1xx_) {
    // This is 100 Continue, only decode it once to support Expect:100-Continue header.
    decoded_1xx_ = true;
    response_decoder_->decode1xxHeaders(std::move(headers));
  } else if (!is_special_1xx) {
    response_decoder_->decodeHeaders(std::move(headers),
                                     /*end_stream=*/fin);
    if (status == enumToInt(Http::Code::NotModified)) {
      got_304_response_ = true;
    }
  }

  ConsumeHeaderList();
}

void EnvoyQuicClientStream::OnStreamFrame(const quic::QuicStreamFrame& frame) {
  uint64_t highest_byte_received = frame.data_length + frame.offset;
  if (highest_byte_received > bytesMeter()->wireBytesReceived()) {
    mutableBytesMeter()->addWireBytesReceived(highest_byte_received -
                                              bytesMeter()->wireBytesReceived());
  }
  quic::QuicSpdyClientStream::OnStreamFrame(frame);
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
    updateReceivedContentBytes(buffer->length(), fin_read_and_no_trailers);
    if (stream_error() != quic::QUIC_STREAM_NO_ERROR) {
      // A stream error has occurred, stop processing.
      return;
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
  mutableBytesMeter()->addHeaderBytesReceived(frame_len);
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
    updateReceivedContentBytes(0, true);
    if (stream_error() != quic::QUIC_STREAM_NO_ERROR) {
      // A stream error has occurred, stop processing.
      return;
    }
    quic::QuicRstStreamErrorCode transform_rst = quic::QUIC_STREAM_NO_ERROR;
    auto trailers = http2HeaderBlockToEnvoyTrailers<Http::ResponseTrailerMapImpl>(
        received_trailers(), filterManagerConnection()->maxIncomingHeadersCount(), *this, details_,
        transform_rst);
    if (trailers == nullptr) {
      onStreamError(close_connection_upon_invalid_header_, transform_rst);
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

void EnvoyQuicClientStream::ResetWithError(quic::QuicResetStreamError error) {
  ENVOY_STREAM_LOG(debug, "sending reset code={}", *this, error.internal_code());
  stats_.tx_reset_.inc();
  // Upper layers expect calling resetStream() to immediately raise reset callbacks.
  runResetCallbacks(quicRstErrorToEnvoyLocalResetReason(error.internal_code()));
  if (session()->connection()->connected()) {
    quic::QuicSpdyClientStream::ResetWithError(error);
  }
}

void EnvoyQuicClientStream::OnConnectionClosed(quic::QuicErrorCode error,
                                               quic::ConnectionCloseSource source) {
  if (!end_stream_decoded_) {
    runResetCallbacks(
        source == quic::ConnectionCloseSource::FROM_SELF
            ? quicErrorCodeToEnvoyLocalResetReason(error, session()->OneRttKeysAvailable())
            : quicErrorCodeToEnvoyRemoteResetReason(error));
  }
  quic::QuicSpdyClientStream::OnConnectionClosed(error, source);
}

void EnvoyQuicClientStream::OnClose() {
  destroy();
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

bool EnvoyQuicClientStream::hasPendingData() { return BufferedDataBytes() > 0; }

} // namespace Quic
} // namespace Envoy
