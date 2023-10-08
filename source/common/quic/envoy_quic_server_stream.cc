#include "source/common/quic/envoy_quic_server_stream.h"

#include <openssl/bio.h>
#include <openssl/evp.h>

#include <memory>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"
#include "source/common/quic/envoy_quic_server_session.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_stats_gatherer.h"

#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/spdy/core/http2_header_block.h"
#include "quiche_platform_impl/quiche_mem_slice_impl.h"

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

  stats_gatherer_ = new QuicStatsGatherer(&filterManagerConnection()->dispatcher().timeSource());
  set_ack_listener(stats_gatherer_);
}

void EnvoyQuicServerStream::encode1xxHeaders(const Http::ResponseHeaderMap& headers) {
  ASSERT(Http::HeaderUtility::isSpecial1xx(headers));
  encodeHeaders(headers, false);
}

void EnvoyQuicServerStream::encodeHeaders(const Http::ResponseHeaderMap& headers, bool end_stream) {
  ENVOY_STREAM_LOG(debug, "encodeHeaders (end_stream={}) {}.", *this, end_stream, headers);
  if (write_side_closed()) {
    IS_ENVOY_BUG("encodeHeaders is called on write-closed stream.");
    return;
  }

  // In Envoy, both upgrade requests and extended CONNECT requests are
  // represented as their HTTP/1 forms, regardless of the HTTP version used.
  // Therefore, these need to be transformed into their HTTP/3 form, before
  // sending them.
  const Http::ResponseHeaderMap* header_map = &headers;
  std::unique_ptr<Http::ResponseHeaderMapImpl> modified_headers;
#ifndef ENVOY_ENABLE_UHV
  // Extended CONNECT to H/1 upgrade transformation has moved to UHV
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.use_http3_header_normalisation") &&
      Http::Utility::isUpgrade(headers)) {
    modified_headers = Http::createHeaderMap<Http::ResponseHeaderMapImpl>(headers);
    Http::Utility::transformUpgradeResponseFromH1toH3(*modified_headers);
    header_map = modified_headers.get();
  }
#endif
  // This is counting not serialized bytes in the send buffer.
  local_end_stream_ = end_stream;
  SendBufferMonitor::ScopedWatermarkBufferUpdater updater(this, this);
  {
    IncrementalBytesSentTracker tracker(*this, *mutableBytesMeter(), true);
    size_t bytes_sent =
        WriteHeaders(envoyHeadersToHttp2HeaderBlock(*header_map), end_stream, nullptr);
    stats_gatherer_->addBytesSent(bytes_sent, end_stream);
    ENVOY_BUG(bytes_sent != 0, "Failed to encode headers.");
  }

  if (local_end_stream_) {
    if (codec_callbacks_) {
      codec_callbacks_->onCodecEncodeComplete();
    }
    onLocalEndStream();
  }
}

void EnvoyQuicServerStream::encodeData(Buffer::Instance& data, bool end_stream) {
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
#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  if (http_datagram_handler_) {
    IncrementalBytesSentTracker tracker(*this, *mutableBytesMeter(), false);
    if (!http_datagram_handler_->encodeCapsuleFragment(data.toString(), end_stream)) {
      Reset(quic::QUIC_BAD_APPLICATION_PAYLOAD);
      return;
    }
  } else {
#endif
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
      stats_gatherer_->addBytesSent(result.bytes_consumed, end_stream);
    }
    // QUIC stream must take all.
    if (result.bytes_consumed == 0 && has_data) {
      IS_ENVOY_BUG(fmt::format("Send buffer didn't take all the data. Stream is write {} with {} "
                               "bytes in send buffer. Current write was rejected.",
                               write_side_closed() ? "closed" : "open", BufferedDataBytes()));
      Reset(quic::QUIC_BAD_APPLICATION_PAYLOAD);
      return;
    }
#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  }
#endif
  if (local_end_stream_) {
    if (codec_callbacks_) {
      codec_callbacks_->onCodecEncodeComplete();
    }
    onLocalEndStream();
  }
}

void EnvoyQuicServerStream::encodeTrailers(const Http::ResponseTrailerMap& trailers) {
  ENVOY_STREAM_LOG(debug, "encodeTrailers: {}.", *this, trailers);
  if (write_side_closed()) {
    IS_ENVOY_BUG("encodeTrailers is called on write-closed stream.");
    return;
  }
  ASSERT(!local_end_stream_);
  local_end_stream_ = true;

  SendBufferMonitor::ScopedWatermarkBufferUpdater updater(this, this);

  {
    IncrementalBytesSentTracker tracker(*this, *mutableBytesMeter(), true);
    size_t bytes_sent = WriteTrailers(envoyHeadersToHttp2HeaderBlock(trailers), nullptr);
    ENVOY_BUG(bytes_sent != 0, "Failed to encode trailers.");
    stats_gatherer_->addBytesSent(bytes_sent, true);
  }
  if (codec_callbacks_) {
    codec_callbacks_->onCodecEncodeComplete();
  }
  onLocalEndStream();
}

void EnvoyQuicServerStream::encodeMetadata(const Http::MetadataMapVector& /*metadata_map_vector*/) {
  // Metadata Frame is not supported in QUIC.
  ENVOY_STREAM_LOG(debug, "METADATA is not supported in Http3.", *this);
  stats_.metadata_not_supported_error_.inc();
}

void EnvoyQuicServerStream::resetStream(Http::StreamResetReason reason) {
  if (buffer_memory_account_) {
    buffer_memory_account_->clearDownstream();
  }

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
  mutableBytesMeter()->addHeaderBytesReceived(frame_len);
  // TODO(danzh) Fix in QUICHE. If the stream has been reset in the call stack,
  // OnInitialHeadersComplete() shouldn't be called.
  if (read_side_closed()) {
    return;
  }
  quic::QuicSpdyServerStreamBase::OnInitialHeadersComplete(fin, frame_len, header_list);
  if (read_side_closed()) {
    return;
  }

  if (!headers_decompressed() || header_list.empty()) {
    onStreamError(absl::nullopt);
    return;
  }

  ENVOY_STREAM_LOG(debug, "Received headers: {}.", *this, header_list.DebugString());
  if (fin) {
    end_stream_decoded_ = true;
  }
  saw_regular_headers_ = false;
  quic::QuicRstStreamErrorCode rst = quic::QUIC_STREAM_NO_ERROR;
  auto server_session = static_cast<EnvoyQuicServerSession*>(session());
  std::unique_ptr<Http::RequestHeaderMapImpl> headers =
      quicHeadersToEnvoyHeaders<Http::RequestHeaderMapImpl>(
          header_list, *this, server_session->max_inbound_header_list_size(),
          filterManagerConnection()->maxIncomingHeadersCount(), details_, rst);
  if (headers == nullptr) {
    onStreamError(close_connection_upon_invalid_header_, rst);
    return;
  }

#ifndef ENVOY_ENABLE_UHV
  // These checks are now part of UHV
  if (Http::HeaderUtility::checkRequiredRequestHeaders(*headers) != Http::okStatus() ||
      Http::HeaderUtility::checkValidRequestHeaders(*headers) != Http::okStatus() ||
      (headers->Protocol() && !spdy_session()->allow_extended_connect())) {
    details_ = Http3ResponseCodeDetailValues::invalid_http_header;
    onStreamError(absl::nullopt);
    return;
  }

  // Extended CONNECT to H/1 upgrade transformation has moved to UHV
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.use_http3_header_normalisation") &&
      Http::Utility::isH3UpgradeRequest(*headers)) {
    // Transform Request from H3 to H1
    Http::Utility::transformUpgradeRequestFromH3toH1(*headers);
  }
#else
  if (Http::HeaderUtility::checkRequiredRequestHeaders(*headers) != Http::okStatus() ||
      (headers->Protocol() && !spdy_session()->allow_extended_connect())) {
    details_ = Http3ResponseCodeDetailValues::invalid_http_header;
    onStreamError(absl::nullopt);
    return;
  }
#endif

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.enable_connect_udp_support") &&
      (Http::HeaderUtility::isCapsuleProtocol(*headers) ||
       Http::HeaderUtility::isConnectUdpRequest(*headers))) {
    useCapsuleProtocol();
    // HTTP/3 Datagrams sent over CONNECT-UDP are already congestion controlled, so make it bypass
    // the default Datagram queue.
    if (Http::HeaderUtility::isConnectUdpRequest(*headers)) {
      session()->SetForceFlushForDefaultQueue(true);
    }
  }
#endif

  request_decoder_->decodeHeaders(std::move(headers), /*end_stream=*/fin);
  ConsumeHeaderList();
}

void EnvoyQuicServerStream::OnStreamFrame(const quic::QuicStreamFrame& frame) {
  uint64_t highest_byte_received = frame.data_length + frame.offset;
  if (highest_byte_received > bytesMeter()->wireBytesReceived()) {
    mutableBytesMeter()->addWireBytesReceived(highest_byte_received -
                                              bytesMeter()->wireBytesReceived());
  }
  quic::QuicSpdyServerStreamBase::OnStreamFrame(frame);
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
    updateReceivedContentBytes(buffer->length(), fin_read_and_no_trailers);
    if (stream_error() != quic::QUIC_STREAM_NO_ERROR) {
      // A stream error has occurred, stop processing.
      return;
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
  mutableBytesMeter()->addHeaderBytesReceived(frame_len);
  ENVOY_STREAM_LOG(debug, "Received trailers: {}.", *this, received_trailers().DebugString());
  quic::QuicSpdyServerStreamBase::OnTrailingHeadersComplete(fin, frame_len, header_list);
  if (read_side_closed()) {
    return;
  }
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
    updateReceivedContentBytes(0, true);
    if (stream_error() != quic::QUIC_STREAM_NO_ERROR) {
      // A stream error has occurred, stop processing.
      return;
    }
    quic::QuicRstStreamErrorCode rst = quic::QUIC_STREAM_NO_ERROR;
    auto server_session = static_cast<EnvoyQuicServerSession*>(session());
    auto trailers = http2HeaderBlockToEnvoyTrailers<Http::RequestTrailerMapImpl>(
        received_trailers(), server_session->max_inbound_header_list_size(),
        filterManagerConnection()->maxIncomingHeadersCount(), *this, details_, rst);
    if (trailers == nullptr) {
      onStreamError(close_connection_upon_invalid_header_, rst);
      return;
    }
    request_decoder_->decodeTrailers(std::move(trailers));
    MarkTrailersConsumed();
  }
}

bool EnvoyQuicServerStream::OnStopSending(quic::QuicResetStreamError error) {
  // Only called in IETF Quic to close write side.
  ENVOY_STREAM_LOG(debug, "received STOP_SENDING with reset code={}", *this, error.internal_code());
  stats_.rx_reset_.inc();
  bool end_stream_encoded = local_end_stream_;
  // This call will close write.
  if (!quic::QuicSpdyServerStreamBase::OnStopSending(error)) {
    return false;
  }
  ASSERT(write_side_closed());
  // Also stop reading because the peer already didn't care about the response any more.
  if (!reading_stopped()) {
    StopReading();
  }
  if (!end_stream_encoded) {
    // If both directions are closed but end stream hasn't been encoded yet, notify reset callbacks.
    // Treat this as a remote reset, since the stream will be closed in both directions.
    runResetCallbacks(quicRstErrorToEnvoyRemoteResetReason(error.internal_code()));
  }
  return true;
}

void EnvoyQuicServerStream::OnStreamReset(const quic::QuicRstStreamFrame& frame) {
  ENVOY_STREAM_LOG(debug, "received RESET_STREAM with reset code={}", *this, frame.error_code);
  stats_.rx_reset_.inc();
  bool end_stream_decoded_and_encoded = read_side_closed() && local_end_stream_;
  // This closes read side in IETF Quic, but doesn't close write side.
  quic::QuicSpdyServerStreamBase::OnStreamReset(frame);
  ASSERT(read_side_closed());
  if (write_side_closed() && !end_stream_decoded_and_encoded) {
    // If both directions are closed but upstream hasn't received or sent end stream, run reset
    // stream callback.
    runResetCallbacks(quicRstErrorToEnvoyRemoteResetReason(frame.error_code));
  }
}

void EnvoyQuicServerStream::ResetWithError(quic::QuicResetStreamError error) {
  ENVOY_STREAM_LOG(debug, "sending reset code={}", *this, error.internal_code());
  stats_.tx_reset_.inc();
  if (!local_end_stream_) {
    // Upper layers expect calling resetStream() to immediately raise reset callbacks.
    runResetCallbacks(quicRstErrorToEnvoyLocalResetReason(error.internal_code()));
  }
  quic::QuicSpdyServerStreamBase::ResetWithError(error);
}

void EnvoyQuicServerStream::OnConnectionClosed(quic::QuicErrorCode error,
                                               quic::ConnectionCloseSource source) {
  // Run reset callback before closing the stream so that the watermark change will not trigger
  // callbacks.
  if (!local_end_stream_) {
    runResetCallbacks(
        source == quic::ConnectionCloseSource::FROM_SELF
            ? quicErrorCodeToEnvoyLocalResetReason(error, session()->OneRttKeysAvailable())
            : quicErrorCodeToEnvoyRemoteResetReason(error));
  }
  quic::QuicSpdyServerStreamBase::OnConnectionClosed(error, source);
}

void EnvoyQuicServerStream::CloseWriteSide() {
  // Clear the downstream since the stream should not write additional data
  // after this is called, e.g. cannot reset the stream.
  // Only the downstream stream should clear the downstream of the
  // memory account.
  //
  // There are cases where a corresponding upstream stream dtor might
  // be called, but the downstream stream isn't going to terminate soon
  // such as StreamDecoderFilterCallbacks::recreateStream().
  if (buffer_memory_account_) {
    buffer_memory_account_->clearDownstream();
  }
  quic::QuicSpdyServerStreamBase::CloseWriteSide();
}

void EnvoyQuicServerStream::OnClose() {
  destroy();
  quic::QuicSpdyServerStreamBase::OnClose();
  if (isDoingWatermarkAccounting()) {
    return;
  }
  clearWatermarkBuffer();
  if (!stats_gatherer_->loggingDone()) {
    stats_gatherer_->maybeDoDeferredLog(/* record_ack_timing */ false);
  }
  stats_gatherer_ = nullptr;
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
EnvoyQuicServerStream::validateHeader(absl::string_view header_name,
                                      absl::string_view header_value) {
  Http::HeaderUtility::HeaderValidationResult result =
      EnvoyQuicStream::validateHeader(header_name, header_value);
  if (result != Http::HeaderUtility::HeaderValidationResult::ACCEPT) {
    return result;
  }
  // Do request specific checks.
  result = Http::HeaderUtility::checkHeaderNameForUnderscores(
      header_name, headers_with_underscores_action_, stats_);
  if (result != Http::HeaderUtility::HeaderValidationResult::ACCEPT) {
    details_ = Http3ResponseCodeDetailValues::invalid_underscore;
    return result;
  }
  ASSERT(!header_name.empty());
  if (!Http::HeaderUtility::isPseudoHeader(header_name)) {
    return result;
  }
  static const absl::flat_hash_set<std::string> known_pseudo_headers{":authority", ":protocol",
                                                                     ":path", ":method", ":scheme"};
  if (header_name == ":path") {
    if (saw_path_) {
      // According to RFC9114, :path header should only have one value.
      return Http::HeaderUtility::HeaderValidationResult::REJECT;
    }
    saw_path_ = true;
  } else if (!known_pseudo_headers.contains(header_name)) {
    return Http::HeaderUtility::HeaderValidationResult::REJECT;
  }
  return result;
}

void EnvoyQuicServerStream::onStreamError(absl::optional<bool> should_close_connection,
                                          quic::QuicRstStreamErrorCode rst) {
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
    stream_delegate()->OnStreamError(quic::QUIC_HTTP_FRAME_ERROR, std::string(details_));
  } else {
    Reset(rst);
  }
}

void EnvoyQuicServerStream::onPendingFlushTimer() {
  ENVOY_STREAM_LOG(debug, "pending stream flush timeout", *this);
  Http::MultiplexedStreamImplBase::onPendingFlushTimer();
  stats_.tx_flush_timeout_.inc();
  ASSERT(local_end_stream_ && !fin_sent());
  // Reset the stream locally. But no reset callbacks will be run because higher layers think the
  // stream is already finished.
  Reset(quic::QUIC_STREAM_CANCELLED);
}

bool EnvoyQuicServerStream::hasPendingData() {
  // Quic stream sends headers and trailers on the same stream, and buffers them in the same sending
  // buffer if needed. So checking this buffer is sufficient.
  return (!write_side_closed()) && BufferedDataBytes() > 0;
}

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
void EnvoyQuicServerStream::useCapsuleProtocol() {
  http_datagram_handler_ = std::make_unique<HttpDatagramHandler>(*this);
  ASSERT(request_decoder_ != nullptr);
  http_datagram_handler_->setStreamDecoder(request_decoder_);
}
#endif

void EnvoyQuicServerStream::OnInvalidHeaders() { onStreamError(absl::nullopt); }

} // namespace Quic
} // namespace Envoy
