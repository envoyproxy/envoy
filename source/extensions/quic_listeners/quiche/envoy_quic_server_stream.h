#pragma once

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#include "quiche/quic/core/http/quic_spdy_server_stream_base.h"

#pragma GCC diagnostic pop

#include "extensions/quic_listeners/quiche/envoy_quic_stream.h"

namespace Envoy {
namespace Quic {

// This class is a quic stream and also a response encoder.
class EnvoyQuicServerStream : public quic::QuicSpdyServerStreamBase, public EnvoyQuicStream {
public:
  EnvoyQuicServerStream(quic::QuicStreamId id, quic::QuicSpdySession* session,
                        quic::StreamType type)
      : quic::QuicSpdyServerStreamBase(id, session, type) {}
  EnvoyQuicServerStream(quic::PendingStream* pending, quic::QuicSpdySession* session,
                        quic::StreamType type)
      : quic::QuicSpdyServerStreamBase(pending, session, type) {}

  // Http::StreamEncoder
  void encode100ContinueHeaders(const Http::HeaderMap& headers) override;
  void encodeHeaders(const Http::HeaderMap& headers, bool end_stream) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeTrailers(const Http::HeaderMap& trailers) override;
  void encodeMetadata(const Http::MetadataMapVector& metadata_map_vector) override;

  // Http::Stream
  void resetStream(Http::StreamResetReason reason) override;
  void readDisable(bool disable) override;
  // quic::QuicSpdyStream
  void OnBodyAvailable() override;
  void OnStreamReset(const quic::QuicRstStreamFrame& frame) override;
  // quic::QuicServerSessionBase
  void OnConnectionClosed(quic::QuicErrorCode error, quic::ConnectionCloseSource source) override;

protected:
  // quic::QuicSpdyStream
  void OnInitialHeadersComplete(bool fin, size_t frame_len,
                                const quic::QuicHeaderList& header_list) override;
  void OnTrailingHeadersComplete(bool fin, size_t frame_len,
                                 const quic::QuicHeaderList& header_list) override;
};

} // namespace Quic
} // namespace Envoy
