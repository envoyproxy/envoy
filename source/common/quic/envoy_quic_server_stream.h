#pragma once

#include "source/common/quic/envoy_quic_stream.h"

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
#include "source/common/quic/http_datagram_handler.h"
#endif

#include "quiche/common/platform/api/quiche_reference_counted.h"
#include "quiche/quic/core/http/quic_spdy_server_stream_base.h"
#include "quiche/quic/core/qpack/qpack_encoder.h"
#include "quiche/quic/core/qpack/qpack_instruction_encoder.h"

namespace Envoy {
namespace Quic {

// This class is a quic stream and also a response encoder.
class EnvoyQuicServerStream : public quic::QuicSpdyServerStreamBase,
                              public EnvoyQuicStream,
                              public Http::ResponseEncoder,
                              public quic::QuicSpdyStream::MetadataVisitor {
public:
  EnvoyQuicServerStream(quic::QuicStreamId id, quic::QuicSpdySession* session,
                        quic::StreamType type, Http::Http3::CodecStats& stats,
                        const envoy::config::core::v3::Http3ProtocolOptions& http3_options,
                        envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
                            headers_with_underscores_action);

  void setRequestDecoder(Http::RequestDecoder& decoder) override {
    request_decoder_ = &decoder;
    stats_gatherer_->setAccessLogHandlers(request_decoder_->accessLogHandlers());
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

  // quic::QuicStream
  void OnStreamFrame(const quic::QuicStreamFrame& frame) override;
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
  void useCapsuleProtocol();
#endif

  Http::RequestDecoder* request_decoder_{nullptr};
  envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
      headers_with_underscores_action_;

  // True if a :path header has been seen before.
  bool saw_path_{false};
};

} // namespace Quic
} // namespace Envoy
