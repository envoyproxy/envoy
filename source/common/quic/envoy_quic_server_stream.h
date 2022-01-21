#pragma once

#include "source/common/quic/envoy_quic_stream.h"

#include "quiche/quic/core/http/quic_spdy_server_stream_base.h"

namespace Envoy {
namespace Quic {

// This class is a quic stream and also a response encoder.
class EnvoyQuicServerStream : public quic::QuicSpdyServerStreamBase,
                              public EnvoyQuicStream,
                              public Http::ResponseEncoder {
public:
  EnvoyQuicServerStream(quic::QuicStreamId id, quic::QuicSpdySession* session,
                        quic::StreamType type, Http::Http3::CodecStats& stats,
                        const envoy::config::core::v3::Http3ProtocolOptions& http3_options,
                        envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
                            headers_with_underscores_action);

  void setRequestDecoder(Http::RequestDecoder& decoder) { request_decoder_ = &decoder; }

  // Http::StreamEncoder
  void encode1xxHeaders(const Http::ResponseHeaderMap& headers) override;
  void encodeHeaders(const Http::ResponseHeaderMap& headers, bool end_stream) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeTrailers(const Http::ResponseTrailerMap& trailers) override;
  void encodeMetadata(const Http::MetadataMapVector& metadata_map_vector) override;
  Http::Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() override {
    return absl::nullopt;
  }
  bool streamErrorOnInvalidHttpMessage() const override {
    return http3_options_.override_stream_error_on_invalid_http_message().value();
  }

  // Http::Stream
  void resetStream(Http::StreamResetReason reason) override;

  // quic::QuicSpdyStream
  void OnBodyAvailable() override;
  bool OnStopSending(quic::QuicResetStreamError error) override;
  void OnStreamReset(const quic::QuicRstStreamFrame& frame) override;
  void ResetWithError(quic::QuicResetStreamError error) override;
  void OnClose() override;
  void OnCanWrite() override;
  // quic::QuicSpdyServerStreamBase
  void OnConnectionClosed(quic::QuicErrorCode error, quic::ConnectionCloseSource source) override;
  void CloseWriteSide() override;

  void clearWatermarkBuffer();

  // EnvoyQuicStream
  Http::HeaderUtility::HeaderValidationResult
  validateHeader(absl::string_view header_name, absl::string_view header_value) override;

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

  Http::RequestDecoder* request_decoder_{nullptr};
  envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
      headers_with_underscores_action_;
};

} // namespace Quic
} // namespace Envoy
