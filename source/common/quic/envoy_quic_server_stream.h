#pragma once

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "quiche/quic/core/http/quic_spdy_server_stream_base.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "common/quic/envoy_quic_stream.h"

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

  EnvoyQuicServerStream(quic::PendingStream* pending, quic::QuicSpdySession* session,
                        quic::StreamType type, Http::Http3::CodecStats& stats,
                        const envoy::config::core::v3::Http3ProtocolOptions& http3_options,
                        envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
                            headers_with_underscores_action);

  void setRequestDecoder(Http::RequestDecoder& decoder) { request_decoder_ = &decoder; }

  // Http::StreamEncoder
  void encode100ContinueHeaders(const Http::ResponseHeaderMap& headers) override;
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
  void setFlushTimeout(std::chrono::milliseconds) override {
    // TODO(mattklein123): Actually implement this for HTTP/3 similar to HTTP/2.
  }
  Buffer::BufferMemoryAccountSharedPtr getAccount() const override {
    // TODO(kbaichoo): implement account tracking for QUIC.
    return nullptr;
  }

  void setAccount(Buffer::BufferMemoryAccountSharedPtr) override {
    // TODO(kbaichoo): implement account tracking for QUIC.
  }
  // quic::QuicSpdyStream
  void OnBodyAvailable() override;
  bool OnStopSending(quic::QuicRstStreamErrorCode error) override;
  void OnStreamReset(const quic::QuicRstStreamFrame& frame) override;
  void Reset(quic::QuicRstStreamErrorCode error) override;
  void OnClose() override;
  void OnCanWrite() override;
  // quic::QuicSpdyServerStreamBase
  void OnConnectionClosed(quic::QuicErrorCode error, quic::ConnectionCloseSource source) override;

  void clearWatermarkBuffer();

  // EnvoyQuicStream
  Http::HeaderUtility::HeaderValidationResult
  validateHeader(const std::string& header_name, absl::string_view header_value) override;

protected:
  // EnvoyQuicStream
  void switchStreamBlockState(bool should_block) override;
  uint32_t streamId() override;
  Network::Connection* connection() override;

  // quic::QuicSpdyStream
  void OnInitialHeadersComplete(bool fin, size_t frame_len,
                                const quic::QuicHeaderList& header_list) override;
  void OnTrailingHeadersComplete(bool fin, size_t frame_len,
                                 const quic::QuicHeaderList& header_list) override;
  void OnHeadersTooLarge() override;

private:
  QuicFilterManagerConnectionImpl* filterManagerConnection();

  // Deliver awaiting trailers if body has been delivered.
  void maybeDecodeTrailers();

  // Either reset the stream or close the connection according to
  // should_close_connection and configured http3 options.
  void onStreamError(absl::optional<bool> should_close_connection);

  Http::RequestDecoder* request_decoder_{nullptr};
  envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
      headers_with_underscores_action_;
};

} // namespace Quic
} // namespace Envoy
