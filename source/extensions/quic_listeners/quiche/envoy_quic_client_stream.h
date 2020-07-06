#pragma once

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#include "quiche/quic/core/http/quic_spdy_client_stream.h"

#pragma GCC diagnostic pop

#include "extensions/quic_listeners/quiche/envoy_quic_stream.h"

namespace Envoy {
namespace Quic {

// This class is a quic stream and also a request encoder.
class EnvoyQuicClientStream : public quic::QuicSpdyClientStream,
                              public EnvoyQuicStream,
                              public Http::RequestEncoder {
public:
  EnvoyQuicClientStream(quic::QuicStreamId id, quic::QuicSpdyClientSession* client_session,
                        quic::StreamType type);
  EnvoyQuicClientStream(quic::PendingStream* pending, quic::QuicSpdyClientSession* client_session,
                        quic::StreamType type);

  void setResponseDecoder(Http::ResponseDecoder& decoder) { response_decoder_ = &decoder; }

  // Http::StreamEncoder
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeMetadata(const Http::MetadataMapVector& metadata_map_vector) override;
  Http::Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() override {
    return absl::nullopt;
  }

  // Http::RequestEncoder
  void encodeHeaders(const Http::RequestHeaderMap& headers, bool end_stream) override;
  void encodeTrailers(const Http::RequestTrailerMap& trailers) override;

  // Http::Stream
  void resetStream(Http::StreamResetReason reason) override;
  void setFlushTimeout(std::chrono::milliseconds) override {}
  // quic::QuicSpdyStream
  void OnBodyAvailable() override;
  void OnStreamReset(const quic::QuicRstStreamFrame& frame) override;
  void OnClose() override;
  void OnCanWrite() override;
  // quic::Stream
  void OnConnectionClosed(quic::QuicErrorCode error, quic::ConnectionCloseSource source) override;

protected:
  // EnvoyQuicStream
  void switchStreamBlockState(bool should_block) override;
  uint32_t streamId() override;
  Network::Connection* connection() override;

  // quic::QuicSpdyStream
  // Overridden to pass headers to decoder.
  void OnInitialHeadersComplete(bool fin, size_t frame_len,
                                const quic::QuicHeaderList& header_list) override;
  void OnTrailingHeadersComplete(bool fin, size_t frame_len,
                                 const quic::QuicHeaderList& header_list) override;

private:
  QuicFilterManagerConnectionImpl* filterManagerConnection();

  Http::ResponseDecoder* response_decoder_{nullptr};
};

} // namespace Quic
} // namespace Envoy
