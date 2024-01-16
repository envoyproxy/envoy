#pragma once

#include "envoy/buffer/buffer.h"

#include "source/common/quic/envoy_quic_stream.h"

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
#include "source/common/quic/http_datagram_handler.h"
#endif
#include "quiche/common/simple_buffer_allocator.h"
#include "quiche/quic/core/http/quic_spdy_client_stream.h"

namespace Envoy {
namespace Quic {

// This class is a quic stream and also a request encoder.
class EnvoyQuicClientStream : public quic::QuicSpdyClientStream,
                              public EnvoyQuicStream,
                              public Http::RequestEncoder {
public:
  EnvoyQuicClientStream(quic::QuicStreamId id, quic::QuicSpdyClientSession* client_session,
                        quic::StreamType type, Http::Http3::CodecStats& stats,
                        const envoy::config::core::v3::Http3ProtocolOptions& http3_options);

  void setResponseDecoder(Http::ResponseDecoder& decoder) { response_decoder_ = &decoder; }

  // Http::StreamEncoder
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeMetadata(const Http::MetadataMapVector& metadata_map_vector) override;
  Http::Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() override {
    return absl::nullopt;
  }

  // Http::RequestEncoder
  Http::Status encodeHeaders(const Http::RequestHeaderMap& headers, bool end_stream) override;
  void encodeTrailers(const Http::RequestTrailerMap& trailers) override;
  void enableTcpTunneling() override {}

  // Http::Stream
  void resetStream(Http::StreamResetReason reason) override;
  void setFlushTimeout(std::chrono::milliseconds) override {}

  // quic::QuicStream
  void OnStreamFrame(const quic::QuicStreamFrame& frame) override;
  bool OnStopSending(quic::QuicResetStreamError error) override;
  // quic::QuicSpdyStream
  void OnBodyAvailable() override;
  void OnStreamReset(const quic::QuicRstStreamFrame& frame) override;
  void ResetWithError(quic::QuicResetStreamError error) override;
  void OnClose() override;
  void OnCanWrite() override;
  // quic::Stream
  void OnConnectionClosed(quic::QuicErrorCode error, quic::ConnectionCloseSource source) override;

  void clearWatermarkBuffer();

protected:
  // EnvoyQuicStream
  void switchStreamBlockState() override;
  uint32_t streamId() override;
  Network::Connection* connection() override;

  // quic::QuicSpdyStream
  // Overridden to pass headers to decoder.
  void OnInitialHeadersComplete(bool fin, size_t frame_len,
                                const quic::QuicHeaderList& header_list) override;
  void OnTrailingHeadersComplete(bool fin, size_t frame_len,
                                 const quic::QuicHeaderList& header_list) override;
  void OnInvalidHeaders() override;

  // Http::MultiplexedStreamImplBase
  bool hasPendingData() override;

  void onStreamError(absl::optional<bool> should_close_connection,
                     quic::QuicRstStreamErrorCode rst_code) override;

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

  Http::ResponseDecoder* response_decoder_{nullptr};
  bool decoded_1xx_{false};
#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  // Setting |http_datagram_handler_| enables HTTP Datagram support.
  std::unique_ptr<HttpDatagramHandler> http_datagram_handler_;
#endif

  // When an HTTP Upgrade is requested, this contains the protocol upgrade type, e.g. "websocket".
  // It will be empty, when no such request is active.
  std::string upgrade_protocol_;
};

} // namespace Quic
} // namespace Envoy
