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

#include "extensions/quic_listeners/quiche/envoy_quic_stream.h"

namespace Envoy {
namespace Quic {

// This class is a quic stream and also a response encoder.
class EnvoyQuicServerStream : public quic::QuicSpdyServerStreamBase,
                              public EnvoyQuicStream,
                              public Http::ResponseEncoder {
public:
  EnvoyQuicServerStream(quic::QuicStreamId id, quic::QuicSpdySession* session,
                        quic::StreamType type);

  EnvoyQuicServerStream(quic::PendingStream* pending, quic::QuicSpdySession* session,
                        quic::StreamType type);

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
  bool streamErrorOnInvalidHttpMessage() const override { return false; }

  // Http::Stream
  void resetStream(Http::StreamResetReason reason) override;
  void setFlushTimeout(std::chrono::milliseconds) override {
    // TODO(mattklein123): Actually implement this for HTTP/3 similar to HTTP/2.
  }
  // quic::QuicSpdyStream
  void OnBodyAvailable() override;
  void OnStreamReset(const quic::QuicRstStreamFrame& frame) override;
  void OnClose() override;
  void OnCanWrite() override;
  // quic::QuicServerSessionBase
  void OnConnectionClosed(quic::QuicErrorCode error, quic::ConnectionCloseSource source) override;

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

private:
  QuicFilterManagerConnectionImpl* filterManagerConnection();

  Http::RequestDecoder* request_decoder_{nullptr};
};

} // namespace Quic
} // namespace Envoy
