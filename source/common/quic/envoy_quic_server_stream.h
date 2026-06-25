#pragma once

#include "source/common/quic/envoy_quic_stream.h"

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
#include "source/common/quic/http_datagram_handler.h"
#endif

#include "quiche/common/platform/api/quiche_reference_counted.h"
#include "quiche/quic/core/http/quic_spdy_server_stream_base.h"
#include "quiche/quic/core/http/web_transport_http3.h"
#include "quiche/quic/core/qpack/qpack_encoder.h"
#include "quiche/quic/core/qpack/qpack_instruction_encoder.h"

namespace Envoy {
namespace Quic {

// This class is a quic stream and also a response encoder.
class EnvoyQuicServerStream : public quic::QuicSpdyServerStreamBase,
                              public EnvoyQuicStream,
                              public Http::ResponseEncoder,
                              public quic::QuicSpdyStream::MetadataVisitor,
                              public Http::WebTransportSession {
public:
  EnvoyQuicServerStream(quic::QuicStreamId id, quic::QuicSpdySession* session,
                        quic::StreamType type, Http::Http3::CodecStats& stats,
                        const envoy::config::core::v3::Http3ProtocolOptions& http3_options,
                        envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
                            headers_with_underscores_action);

  void setRequestDecoder(Http::RequestDecoder& decoder) override {
    request_decoder_ = decoder.getRequestDecoderHandle();
    stats_gatherer_->setAccessLogHandlers(request_decoder_->get()->accessLogHandlers());
  }

  // Http::StreamEncoder
  void encode1xxHeaders(const Http::ResponseHeaderMap& headers) override;
  void encodeHeaders(const Http::ResponseHeaderMap& headers, bool end_stream) override;
  void encodeTrailers(const Http::ResponseTrailerMap& trailers) override;
  Http::Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() override {
    return absl::nullopt;
  }
  bool streamErrorOnInvalidHttpMessage() const override {
    return http3_options_.override_stream_error_on_invalid_http_message().value();
  }

  // Accept headers/trailers and stream info from HCM for deferred logging. We pass on the
  // header/trailer shared pointers, but copy the non-shared stream info to avoid lifetime issues if
  // the stream is destroyed before logging is complete.
  void
  setDeferredLoggingHeadersAndTrailers(Http::RequestHeaderMapConstSharedPtr request_header_map,
                                       Http::ResponseHeaderMapConstSharedPtr response_header_map,
                                       Http::ResponseTrailerMapConstSharedPtr response_trailer_map,
                                       StreamInfo::StreamInfo& stream_info) override {
    std::unique_ptr<StreamInfo::StreamInfoImpl> new_stream_info =
        std::make_unique<StreamInfo::StreamInfoImpl>(
            filterManagerConnection()->dispatcher().timeSource(),
            filterManagerConnection()->connectionInfoProviderSharedPtr(),
            StreamInfo::FilterState::LifeSpan::FilterChain);
    new_stream_info->setFrom(stream_info, request_header_map.get());
    stats_gatherer_->setDeferredLoggingHeadersAndTrailers(
        request_header_map, response_header_map, response_trailer_map, std::move(new_stream_info));
  };

  // Http::Stream
  void resetStream(Http::StreamResetReason reason) override;
  absl::optional<uint32_t> codecStreamId() const override { return id(); }
#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  // Exposes the downstream WebTransport session (created by QUICHE for a negotiated WebTransport
  // CONNECT) so the upstream codec can bridge it to the upstream session. See
  // Http::StreamDecoderFilterCallbacks::webTransportSession().
  OptRef<Http::WebTransportSession> webTransportSession() override {
    if (auto* session = web_transport(); session != nullptr) {
      return *this;
    }
    return {};
  }
#endif

  // quic::QuicStream
  void OnStreamFrame(const quic::QuicStreamFrame& frame) override;
  void OnSoonToBeDestroyed() override;
  // quic::QuicSpdyStream
  void OnBodyAvailable() override;
  bool OnStopSending(quic::QuicResetStreamError error) override;
  void OnStreamReset(const quic::QuicRstStreamFrame& frame) override;
  void ResetWithError(quic::QuicResetStreamError error) override;
  void OnClose() override;
  void OnCanWrite() override;
  // quic::QuicSpdyServerStreamBase
  void OnConnectionClosed(const quic::QuicConnectionCloseFrame& frame,
                          quic::ConnectionCloseSource source) override;
  void CloseWriteSide() override;

  void clearWatermarkBuffer();

  // EnvoyQuicStream
  Http::HeaderUtility::HeaderValidationResult
  validateHeader(absl::string_view header_name, absl::string_view header_value) override;

  // quic::QuicSpdyStream::MetadataVisitor
  void OnMetadataComplete(size_t frame_len, const quic::QuicHeaderList& header_list) override;

protected:
  void setWebTransportVisitor(std::unique_ptr<webtransport::SessionVisitor> visitor) override {
    if (auto* session = web_transport(); session != nullptr) {
      session->SetVisitor(std::move(visitor));
    }
  }
  webtransport::Session* rawWebTransportSession() override { return web_transport(); }

  // EnvoyQuicStream
  void switchStreamBlockState() override;
  uint32_t streamId() override;
  Network::Connection* connection() override;

  // quic::QuicSpdyStream
  void OnInitialHeadersComplete(bool fin, size_t frame_len,
                                const quic::QuicHeaderList& header_list) override;
  void OnTrailingHeadersComplete(bool fin, size_t frame_len,
                                 const quic::QuicHeaderList& header_list) override;
  void OnHeadersTooLarge() override;
  void OnInvalidHeaders() override;

  // Http::MultiplexedStreamImplBase
  void onPendingFlushTimer() override;
  bool hasPendingData() override;

  void
  onStreamError(absl::optional<bool> should_close_connection,
                quic::QuicRstStreamErrorCode rst = quic::QUIC_BAD_APPLICATION_PAYLOAD) override;

private:
  QuicFilterManagerConnectionImpl* filterManagerConnection();

  // Deliver awaiting trailers if body has been delivered.
  void maybeDecodeTrailers();

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  // Makes the QUIC stream use Capsule Protocol. Once this method is called, any calls to encodeData
  // are expected to contain capsules which will be sent along as HTTP Datagrams. Also, the stream
  // starts to receive HTTP/3 Datagrams and decode into Capsules.
  // Returns true if the capsule protocol handler was installed. Returns false (without installing
  // anything) if a datagram visitor is already registered on the stream (e.g. by the WebTransport
  // session of QUICHE), since registering a second one would be a QUIC_BUG.
  bool useCapsuleProtocol();

  // A WebTransport CONNECT stream carries only the extended-CONNECT HEADERS and then WebTransport
  // capsules/data; HTTP request body, trailers, and METADATA are not part of WebTransport and
  // QUICHE does not reject them on such a stream. If this is a WebTransport stream (web_transport()
  // is non-null), reset just this stream (its WebTransport session) so the offending frame is never
  // proxied upstream, and return true so the caller stops processing. Returns false for non-
  // WebTransport streams, which proceed normally. @param frame_type names the frame for logging.
  bool resetIfWebTransport(absl::string_view frame_type);
#endif

  // Returns the request decoder, or nullptr if one has not been set up yet. The decoder is created
  // lazily once request headers arrive, so a WebTransport data stream (which never receives
  // headers) has no decoder; callers must tolerate nullptr.
  Http::RequestDecoder* requestDecoderOrNull() {
    return request_decoder_ == nullptr ? nullptr : request_decoder_->get().ptr();
  }

  Http::RequestDecoderHandlePtr request_decoder_;
  envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
      headers_with_underscores_action_;

  // True if a :path header has been seen before.
  bool saw_path_{false};
};

} // namespace Quic
} // namespace Envoy
