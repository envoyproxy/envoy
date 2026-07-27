#pragma once

#include <cstdint>

#include "envoy/buffer/buffer.h"

#include "source/common/quic/envoy_quic_stream.h"
#include "source/common/runtime/runtime_features.h"

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
#include "source/common/quic/http_datagram_handler.h"
#endif
#include "quiche/common/simple_buffer_allocator.h"
#include "quiche/quic/core/http/quic_spdy_client_stream.h"
#include "quiche/quic/core/http/web_transport_http3.h"
#include "quiche/quic/core/qpack/qpack_encoder.h"
#include "quiche/quic/core/qpack/qpack_instruction_encoder.h"

namespace Envoy {
namespace Quic {

// This class is a quic stream and also a request encoder.
class EnvoyQuicClientStream : public quic::QuicSpdyClientStream,
                              public EnvoyQuicStream,
                              public Http::RequestEncoder,
                              public quic::QuicSpdyStream::MetadataVisitor,
                              public Http::WebTransportSession {
public:
  EnvoyQuicClientStream(quic::QuicStreamId id, quic::QuicSpdyClientSession* client_session,
                        quic::StreamType type, Http::Http3::CodecStats& stats,
                        const envoy::config::core::v3::Http3ProtocolOptions& http3_options);

  void setResponseDecoder(Http::ResponseDecoder& decoder);

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  // Writes a WebTransport CONNECT request whose encodeHeaders() was deferred until the peer's
  // HTTP/3 SETTINGS arrived (see encodeHeaders()). Called by
  // EnvoyQuicClientSession::OnSettingsFrame(). A no-op if no request is pending.
  void flushPendingHeaders();
#endif

  // Http::StreamEncoder
  Http::Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() override {
    return std::nullopt;
  }

  // Http::RequestEncoder
  Http::Status encodeHeaders(const Http::RequestHeaderMap& headers, bool end_stream) override;
  void encodeTrailers(const Http::RequestTrailerMap& trailers) override;
  void enableTcpTunneling() override {}

  // Http::Stream
  void resetStream(Http::StreamResetReason reason) override;
  void setFlushTimeout(std::chrono::milliseconds) override {}
  std::optional<uint32_t> codecStreamId() const override { return id(); }
#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  // Exposes this stream's WebTransport session (created by QUICHE for a negotiated WebTransport
  // CONNECT) through the codec Stream interface, mirroring EnvoyQuicServerStream. Lets tests reach
  // the session via Http::Stream. Empty OptRef when this is not a WebTransport stream.
  OptRef<Http::WebTransportSession> webTransportSession() override {
    if (auto* session = web_transport(); session != nullptr) {
      return *this;
    }
    return {};
  }
#endif

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
  void OnConnectionClosed(const quic::QuicConnectionCloseFrame& frame,
                          quic::ConnectionCloseSource source) override;

  void clearWatermarkBuffer();

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
  // Overridden to pass headers to decoder.
  void OnInitialHeadersComplete(bool fin, size_t frame_len,
                                const quic::QuicHeaderList& header_list) override;
  void OnTrailingHeadersComplete(bool fin, size_t frame_len,
                                 const quic::QuicHeaderList& header_list) override;
  void OnInvalidHeaders() override;

  // Http::MultiplexedStreamImplBase
  bool hasPendingData() override;

  void onStreamError(std::optional<bool> should_close_connection,
                     quic::QuicRstStreamErrorCode rst_code) override;

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

  // If the headers indicate this is a WebTransport session, initialize the WebTransportSessionState
  // in the upstream filter state so the session can be bridged to the downstream one.
  void mayInitializeWebTransportState(uint64_t status_code);
#endif

  // Returns nullptr if the response decoder has already been destructed.
  Http::ResponseDecoder* getResponseDecoder();

  void onResponseDecoderDead() const;

  Http::ResponseDecoderHandlePtr response_decoder_handle_;
  Http::ResponseDecoder* response_decoder_{nullptr};
  bool decoded_1xx_{false};

  // When an HTTP Upgrade is requested, this contains the protocol upgrade type, e.g. "websocket".
  // It will be empty, when no such request is active.
  std::string upgrade_protocol_;

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  // Set when a WebTransport CONNECT request is buffered in encodeHeaders() awaiting the peer's
  // HTTP/3 SETTINGS frame. Holds a copy of the request headers until flushPendingHeaders() replays
  // encodeHeaders() (or OnClose drops it). Null whenever no such request is pending.
  Http::RequestHeaderMapPtr pending_webtransport_headers_;
  bool pending_webtransport_end_stream_{false};
#endif
};

} // namespace Quic
} // namespace Envoy
