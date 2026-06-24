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
#include "source/common/runtime/runtime_features.h"

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
#include "source/common/quic/web_transport_session_bridge.h"
#endif

#include "quiche/common/http/http_header_block.h"
#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_types.h"

namespace Envoy {
namespace Quic {

EnvoyQuicClientStream::EnvoyQuicClientStream(
    quic::QuicStreamId id, quic::QuicSpdyClientSession* client_session, quic::StreamType type,
    Http::Http3::CodecStats& stats,
    const envoy::config::core::v3::Http3ProtocolOptions& http3_options)
    : quic::QuicSpdyClientStream(id, client_session, type),
      EnvoyQuicStream(
          *this, *client_session,
          // Flow control receive window should be larger than 8k so that the send buffer can fully
          // utilize congestion control window before it reaches the high watermark.
          static_cast<uint32_t>(GetReceiveWindow().value()), *filterManagerConnection(),
          [this]() { runLowWatermarkCallbacks(); }, [this]() { runHighWatermarkCallbacks(); },
          stats, http3_options) {
  ASSERT(static_cast<uint32_t>(GetReceiveWindow().value()) > 8 * 1024,
         "Send buffer limit should be larger than 8KB.");
  RegisterMetadataVisitor(this);
}

void EnvoyQuicClientStream::setResponseDecoder(Http::ResponseDecoder& decoder) {
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.use_response_decoder_handle")) {
    response_decoder_handle_ = decoder.createResponseDecoderHandle();
  }
  response_decoder_ = &decoder;
}

Http::Status EnvoyQuicClientStream::encodeHeaders(const Http::RequestHeaderMap& headers,
                                                  bool end_stream) {
  ENVOY_STREAM_LOG(debug, "encodeHeaders: (end_stream={}) {}.", *this, end_stream, headers);
#ifndef ENVOY_ENABLE_UHV
  // Headers are now validated by UHV before encoding by the codec. Two checks below are not needed
  // when UHV is enabled.
  //
  // Required headers must be present. This can only happen by some erroneous processing after the
  // downstream codecs decode.
  RETURN_IF_ERROR(Http::HeaderUtility::checkRequiredRequestHeaders(headers));
  // Verify that a filter hasn't added an invalid header key or value.
  RETURN_IF_ERROR(Http::HeaderUtility::checkValidRequestHeaders(headers));
#endif

  if (write_side_closed()) {
    return absl::CancelledError("encodeHeaders is called on write-closed stream.");
  }

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  // A WebTransport CONNECT can only be written once the peer's HTTP/3 SETTINGS have been received:
  // QUICHE creates the upstream WebTransport session synchronously inside WriteHeaders, but only if
  // WebTransport is already negotiated (SupportsWebTransport()), which is known only after
  // SETTINGS. If SETTINGS haven't arrived yet, buffer the request and replay this method from
  // flushPendingHeaders() when OnSettingsFrame() fires. Only this one stream waits; every other
  // request on the connection is written immediately.
  if (!spdy_session()->settings_received()) {
    // The headers may have been transformed by UHV or not, so we check both the upgrade and
    // protocol headers for WebTransport values.
    if (headers.getProtocolValue() == Http::Headers::get().ProtocolValues.WebTransport ||
        headers.getUpgradeValue() == Http::Headers::get().ProtocolValues.WebTransport) {
      ENVOY_STREAM_LOG(debug, "Deferring WebTransport CONNECT until peer HTTP/3 SETTINGS received.",
                       *this);
      // TODO(wbpcode): could we avoid this copy by keeping the reference only here because the
      // request headers should never be released before the stream is closed?
      pending_webtransport_headers_ = Http::createHeaderMap<Http::RequestHeaderMapImpl>(headers);
      pending_webtransport_end_stream_ = end_stream;
      static_cast<EnvoyQuicClientSession*>(session())->registerStreamWaitingForSettings(id());
      return Http::okStatus();
    }
  }
#endif

  local_end_stream_ = end_stream;
  SendBufferMonitor::ScopedWatermarkBufferUpdater updater(this, this);
  quiche::HttpHeaderBlock spdy_headers;
#ifndef ENVOY_ENABLE_UHV
  // Extended CONNECT to H/1 upgrade transformation has moved to UHV
  if (Http::Utility::isUpgrade(headers)) {
    // In Envoy, both upgrade requests and extended CONNECT requests are
    // represented as their HTTP/1 forms, regardless of the HTTP version used.
    // Therefore, these need to be transformed into their HTTP/3 form, before
    // sending them.
    upgrade_protocol_ = std::string(headers.getUpgradeValue());
    Http::RequestHeaderMapPtr modified_headers =
        Http::createHeaderMap<Http::RequestHeaderMapImpl>(headers);
    Http::Utility::transformUpgradeRequestFromH1toH3(*modified_headers);
    spdy_headers = envoyHeadersToHttp2HeaderBlock(*modified_headers);
  } else if (headers.Method()) {
    spdy_headers = envoyHeadersToHttp2HeaderBlock(headers);
    if (headers.Method()->value() == "CONNECT") {
      Http::RequestHeaderMapPtr modified_headers =
          Http::createHeaderMap<Http::RequestHeaderMapImpl>(headers);
      modified_headers->remove(Http::Headers::get().Scheme);
      modified_headers->remove(Http::Headers::get().Path);
      modified_headers->remove(Http::Headers::get().Protocol);
      spdy_headers = envoyHeadersToHttp2HeaderBlock(*modified_headers);
    } else if (headers.Method()->value() == "HEAD") {
      sent_head_request_ = true;
    }
  }
  if (spdy_headers.empty()) {
    spdy_headers = envoyHeadersToHttp2HeaderBlock(headers);
  }
#else
  spdy_headers = envoyHeadersToHttp2HeaderBlock(headers);
  if (headers.Method()->value() == "HEAD") {
    sent_head_request_ = true;
  }
#endif
  addDecompressedHeaderBytesSent(spdy_headers);
  {
    IncrementalBytesSentTracker tracker(*this, *mutableBytesMeter(), true);
    size_t bytes_sent = WriteHeaders(std::move(spdy_headers), end_stream, nullptr);
    ENVOY_BUG(bytes_sent != 0, "Failed to encode headers.");
  }

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  if (Http::HeaderUtility::isCapsuleProtocol(headers) ||
      Http::HeaderUtility::isConnectUdpRequest(headers)) {
    // HTTP/3 Datagrams sent over CONNECT-UDP are already congestion controlled, so make it bypass
    // the default Datagram queue.
    if (useCapsuleProtocol()) {
      if (Http::HeaderUtility::isConnectUdpRequest(headers)) {
        session()->SetForceFlushForDefaultQueue(true);
      }
    }
  }
#endif

  if (local_end_stream_) {
    if (codec_callbacks_) {
      codec_callbacks_->onCodecEncodeComplete();
    }
    onLocalEndStream();
  }
  return Http::okStatus();
}

void EnvoyQuicClientStream::encodeTrailers(const Http::RequestTrailerMap& trailers) {
  ENVOY_STREAM_LOG(debug, "encodeTrailers: {}.", *this, trailers);
  quiche::HttpHeaderBlock trailer_block = envoyHeadersToHttp2HeaderBlock(trailers);
  addDecompressedHeaderBytesSent(trailer_block);
  encodeTrailersImpl(std::move(trailer_block));
}

void EnvoyQuicClientStream::resetStream(Http::StreamResetReason reason) {
#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  if (http_datagram_handler_) {
    UnregisterHttp3DatagramVisitor();
  }
#endif
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
  addDecompressedHeaderBytesReceived(header_list);
  if (read_side_closed()) {
    return;
  }
  quic::QuicSpdyStream::OnInitialHeadersComplete(fin, frame_len, header_list);
  if (read_side_closed()) {
    return;
  }

  if (!headers_decompressed() || header_list.empty()) {
    onStreamError(!http3_options_.override_stream_error_on_invalid_http_message().value(),
                  quic::QUIC_BAD_APPLICATION_PAYLOAD);
    return;
  }

  ENVOY_STREAM_LOG(debug, "Received headers: {}.", *this, header_list.DebugString());
  if (fin) {
    end_stream_decoded_ = true;
  }
  saw_regular_headers_ = false;
  quic::QuicRstStreamErrorCode transform_rst = quic::QUIC_STREAM_NO_ERROR;
  auto client_session = static_cast<EnvoyQuicClientSession*>(session());
  std::unique_ptr<Http::ResponseHeaderMapImpl> headers =
      quicHeadersToEnvoyHeaders<Http::ResponseHeaderMapImpl>(
          header_list, *this, client_session->max_inbound_header_list_size(),
          filterManagerConnection()->maxIncomingHeadersCount(), details_, transform_rst);
  if (headers == nullptr) {
    onStreamError(close_connection_upon_invalid_header_, transform_rst);
    return;
  }

  if (fin && Runtime::runtimeFeatureEnabled(
                 "envoy.reloadable_features.quic_validate_headers_only_content_length")) {
    updateReceivedContentBytes(0, true);
    if (stream_error() != quic::QUIC_STREAM_NO_ERROR) {
      return;
    }
  }

  const absl::optional<uint64_t> optional_status =
      Http::Utility::getResponseStatusOrNullopt(*headers);

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  mayInitializeWebTransportState(optional_status.value_or(0));
#endif

#ifndef ENVOY_ENABLE_UHV
  if (!optional_status.has_value()) {
    details_ = Http3ResponseCodeDetailValues::invalid_http_header;
    onStreamError(!http3_options_.override_stream_error_on_invalid_http_message().value(),
                  quic::QUIC_BAD_APPLICATION_PAYLOAD);
    return;
  }

  if (!upgrade_protocol_.empty()) {
    Http::Utility::transformUpgradeResponseFromH3toH1(*headers, upgrade_protocol_);
  }
#else
  // Extended CONNECT to H/1 upgrade transformation has moved to UHV
  // In Envoy, both upgrade requests and extended CONNECT requests are
  // represented as their HTTP/1 forms, regardless of the HTTP version used.
  // Therefore, these need to be transformed into their HTTP/1 form.

  // In UHV mode the :status header at this point can be malformed, as it is validated
  // later on in the response_decoder_.decodeHeaders() call.
  // Account for this here.
  if (!optional_status.has_value()) {
    // In case the status is invalid or missing, the response_decoder_.decodeHeaders() will fail the
    // request
    if (Http::ResponseDecoder* decoder = getResponseDecoder()) {
      decoder->decodeHeaders(std::move(headers), fin);
    } else {
      onResponseDecoderDead();
    }
    ConsumeHeaderList();
    return;
  }
#endif

  const uint64_t status = optional_status.value();
  // TODO(#29071) determine how to handle 101, since it is not supported by HTTP/2
  if (Http::CodeUtility::is1xx(status)) {
    // These are Informational 1xx headers, not the actual response headers.
    set_headers_decompressed(false);
  }

  const bool is_special_1xx = Http::HeaderUtility::isSpecial1xx(*headers);
  if (is_special_1xx && !decoded_1xx_) {
    // This is 100 Continue, only decode it once to support Expect:100-Continue header.
    decoded_1xx_ = true;
    if (Http::ResponseDecoder* decoder = getResponseDecoder()) {
      decoder->decode1xxHeaders(std::move(headers));
    } else {
      onResponseDecoderDead();
    }
  } else if (!is_special_1xx) {
    if (Http::ResponseDecoder* decoder = getResponseDecoder()) {
      decoder->decodeHeaders(std::move(headers),
                             /*end_stream=*/fin);
    } else {
      onResponseDecoderDead();
    }
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

bool EnvoyQuicClientStream::OnStopSending(quic::QuicResetStreamError error) {
  // Only called in IETF Quic to close write side.
  ENVOY_STREAM_LOG(debug, "received STOP_SENDING with reset code={}", *this,
                   static_cast<int>(error.internal_code()));
  bool end_stream_encoded = local_end_stream_;
  // This call will close write.
  if (!quic::QuicSpdyClientStream::OnStopSending(error)) {
    return false;
  }

  stats_.rx_reset_.inc();

  if (read_side_closed() && !end_stream_encoded) {
    // If both directions are closed but end stream hasn't been encoded yet, notify reset callbacks.
    // Treat this as a remote reset, since the stream will be closed in both directions.
    Http::StreamResetReason reason;
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.http_preserve_rst_no_error") &&
        error.internal_code() == quic::QUIC_STREAM_NO_ERROR) {
      reason = Http::StreamResetReason::RemoteResetNoError;
    } else {
      reason = quicRstErrorToEnvoyRemoteResetReason(error.internal_code());
    }
    runResetCallbacks(
        reason,
        absl::StrCat(quic::QuicRstStreamErrorCodeToString(error.internal_code()), "|FROM_PEER"));
  }
  return true;
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
    if (Http::ResponseDecoder* decoder = getResponseDecoder()) {
      decoder->decodeData(*buffer, fin_read_and_no_trailers);
    } else {
      onResponseDecoderDead();
    }
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
  addDecompressedHeaderBytesReceived(header_list);
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
    auto client_session = static_cast<EnvoyQuicClientSession*>(session());
    auto trailers = http2HeaderBlockToEnvoyTrailers<Http::ResponseTrailerMapImpl>(
        received_trailers(), client_session->max_inbound_header_list_size(),
        filterManagerConnection()->maxIncomingHeadersCount(), *this, details_, transform_rst);
    if (trailers == nullptr) {
      onStreamError(close_connection_upon_invalid_header_, transform_rst);
      return;
    }
    if (Http::ResponseDecoder* decoder = getResponseDecoder()) {
      decoder->decodeTrailers(std::move(trailers));
    } else {
      onResponseDecoderDead();
    }
    MarkTrailersConsumed();
  }
}

void EnvoyQuicClientStream::OnStreamReset(const quic::QuicRstStreamFrame& frame) {
  ENVOY_STREAM_LOG(debug, "received reset code={}", *this, static_cast<int>(frame.error_code));
  stats_.rx_reset_.inc();
  // Capture pre-reset state: OnStreamReset closes the read side, so read_side_closed()
  // will be true afterwards regardless of whether the response was fully received.
  bool read_side_was_closed = read_side_closed();
  bool end_stream_decoded_and_encoded = read_side_was_closed && local_end_stream_;
  // This closes read side in IETF Quic, but doesn't close write side.
  quic::QuicSpdyClientStream::OnStreamReset(frame);
  ASSERT(read_side_closed());
  if (write_side_closed() && !end_stream_decoded_and_encoded) {
    Http::StreamResetReason reason;
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.http_preserve_rst_no_error") &&
        frame.error_code == quic::QUIC_STREAM_NO_ERROR && read_side_was_closed) {
      reason = Http::StreamResetReason::RemoteResetNoError;
    } else {
      reason = quicRstErrorToEnvoyRemoteResetReason(frame.error_code);
    }
    runResetCallbacks(
        reason, absl::StrCat(quic::QuicRstStreamErrorCodeToString(frame.error_code), "|FROM_PEER"));
  }
}

void EnvoyQuicClientStream::ResetWithError(quic::QuicResetStreamError error) {
  ENVOY_STREAM_LOG(debug, "sending reset code={}", *this, static_cast<int>(error.internal_code()));
  stats_.tx_reset_.inc();
  filterManagerConnection()->incrementSentQuicResetStreamErrorStats(error, /*from_self*/ true,
                                                                    /*is_upstream*/ true);
  // Upper layers expect calling resetStream() to immediately raise reset callbacks.
  runResetCallbacks(
      quicRstErrorToEnvoyLocalResetReason(error.internal_code()),
      absl::StrCat(quic::QuicRstStreamErrorCodeToString(error.internal_code()), "|FROM_SELF"));
  if (session()->connection()->connected()) {
    quic::QuicSpdyClientStream::ResetWithError(error);
  }
}

void EnvoyQuicClientStream::OnConnectionClosed(const quic::QuicConnectionCloseFrame& frame,
                                               quic::ConnectionCloseSource source) {
  if (!end_stream_decoded_) {
    runResetCallbacks(source == quic::ConnectionCloseSource::FROM_SELF
                          ? quicErrorCodeToEnvoyLocalResetReason(frame.quic_error_code,
                                                                 session()->OneRttKeysAvailable())
                          : quicErrorCodeToEnvoyRemoteResetReason(frame.quic_error_code),
                      absl::StrCat(quic::QuicErrorCodeToString(frame.quic_error_code), "|",
                                   quic::ConnectionCloseSourceToString(source), "|",
                                   frame.error_details));
  }
  quic::QuicSpdyClientStream::OnConnectionClosed(frame, source);
}

void EnvoyQuicClientStream::OnClose() {
#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  if (pending_webtransport_headers_ != nullptr) {
    // The stream is being torn down while still waiting for the peer's SETTINGS to flush a deferred
    // WebTransport CONNECT. Drop the buffered headers and unregister so the session does not flush
    // a destroyed stream.
    pending_webtransport_headers_.reset();
    static_cast<EnvoyQuicClientSession*>(session())->unregisterStreamWaitingForSettings(id());
  }
#endif
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

void EnvoyQuicClientStream::OnMetadataComplete(size_t /*frame_len*/,
                                               const quic::QuicHeaderList& header_list) {
  if (mustRejectMetadata(header_list.uncompressed_header_bytes())) {
    onStreamError(true, quic::QUIC_HEADERS_TOO_LARGE);

    return;
  }
  if (!header_list.empty()) {
    if (Http::ResponseDecoder* decoder = getResponseDecoder()) {
      decoder->decodeMetadata(metadataMapFromHeaderList(header_list));
    } else {
      onResponseDecoderDead();
    }
  }
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

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
// TODO(https://github.com/envoyproxy/envoy/issues/23564): Make the stream use Capsule Protocol
// for CONNECT-UDP support when the headers contain "Capsule-Protocol: ?1" or "Upgrade:
// connect-udp".
bool EnvoyQuicClientStream::useCapsuleProtocol() {
  if (uses_capsules()) {
    // A datagram visitor is already registered on this stream (e.g. the WebTransport session of
    // QUICHE registered itself). Registering another would be a QUIC_BUG, so skip.
    ENVOY_STREAM_LOG(
        warn, "Skipping capsule protocol setup: a datagram visitor is already registered.", *this);
    return false;
  }
  http_datagram_handler_ = std::make_unique<HttpDatagramHandler>(*this);
  http_datagram_handler_->setStreamDecoder(getResponseDecoder());
  RegisterHttp3DatagramVisitor(http_datagram_handler_.get());
  return true;
}

void EnvoyQuicClientStream::mayInitializeWebTransportState(uint64_t status_code) {
  // After QUICHE processes the response headers for a WebTransport CONNECT, it has created the
  // upstream WebTransportHttp3 session. Obtain the downstream session (created earlier, when the
  // downstream CONNECT request arrived) from the paired downstream stream via the response decoder,
  // and bridge the two directly here at the upstream — no filter state involved.

  // web_transport() is non-null only for a negotiated WebTransport CONNECT (QUICHE created the
  // session when these request headers were written). The 2xx check rejects WebTransport CONNECT
  // the upstream declined.
  if (web_transport() == nullptr || !Http::CodeUtility::is2xx(status_code)) {
    return;
  }
  Http::ResponseDecoder* decoder = getResponseDecoder();
  if (decoder == nullptr) {
    return;
  }
  OptRef<Http::WebTransportSession> downstream_session = decoder->downstreamWebTransportSession();
  if (!downstream_session.has_value()) {
    return;
  }

  // installBridge creates the shared state and the two bridges that co-own it; each bridge nulls
  // its side of the state when its session (and thus the bridge) is destroyed, so neither side ever
  // forwards to a freed peer. This stream is itself the upstream Http::WebTransportSession (it
  // implements the interface over its own web_transport()).
  WebTransportSessionBridge::installBridge(downstream_session.ref(), *this);
}

void EnvoyQuicClientStream::flushPendingHeaders() {
  if (pending_webtransport_headers_ == nullptr) {
    return;
  }
  ENVOY_STREAM_LOG(debug, "Flushing deferred WebTransport CONNECT after peer HTTP/3 SETTINGS.",
                   *this);
  Http::RequestHeaderMapPtr headers = std::move(pending_webtransport_headers_);
  // settings_received() is now true, so this replay writes the request instead of deferring again.
  // Ignore the status: the original encodeHeaders() caller already received okStatus, and any error
  // here (e.g. the stream was reset while waiting) is surfaced through the stream's reset
  // callbacks.
  encodeHeaders(*headers, pending_webtransport_end_stream_).IgnoreError();
}
#endif

void EnvoyQuicClientStream::OnInvalidHeaders() {
  onStreamError(absl::nullopt, quic::QUIC_BAD_APPLICATION_PAYLOAD);
}

void EnvoyQuicClientStream::onResponseDecoderDead() const {
  const std::string error_msg = "response_decoder_ use after free detected.";
  IS_ENVOY_BUG(error_msg);
  RELEASE_ASSERT(!Runtime::runtimeFeatureEnabled(
                     "envoy.reloadable_features.abort_when_accessing_dead_decoder"),
                 error_msg);
}

Http::ResponseDecoder* EnvoyQuicClientStream::getResponseDecoder() {
  if (response_decoder_handle_ == nullptr) {
    return response_decoder_;
  }
  if (response_decoder_handle_) {
    if (OptRef<Http::ResponseDecoder> decoder = response_decoder_handle_->get();
        decoder.has_value()) {
      return &decoder.value().get();
    }
  }
  return nullptr;
}

} // namespace Quic
} // namespace Envoy
