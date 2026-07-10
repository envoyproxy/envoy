#include "source/common/quic/client_codec_impl.h"

#include "source/common/quic/envoy_quic_client_stream.h"

namespace Envoy {
namespace Quic {

// Converts a QuicStream instance to EnvoyQuicStream instance. The current stream implementation
// inherits from these two interfaces, with the former one providing Quic interface and the latter
// providing Envoy interface.
EnvoyQuicClientStream* quicStreamToEnvoyClientStream(quic::QuicStream* stream) {
  return dynamic_cast<EnvoyQuicClientStream*>(stream);
}

QuicHttpClientConnectionImpl::QuicHttpClientConnectionImpl(
    EnvoyQuicClientSession& session, Http::ConnectionCallbacks& callbacks,
    Http::Http3::CodecStats& stats,
    const envoy::config::core::v3::Http3ProtocolOptions& http3_options,
    const uint32_t max_request_headers_kb, const uint32_t max_response_headers_count)
    : QuicHttpConnectionImplBase(session, stats), quic_client_session_(session) {
  session.setCodecStats(stats);
  session.setHttp3Options(http3_options);
  session.setHttpConnectionCallbacks(callbacks);
  session.setMaxIncomingHeadersCount(max_response_headers_count);
  session.set_max_inbound_header_list_size(max_request_headers_kb * 1024);
}

void QuicHttpClientConnectionImpl::goAway() {
  quic_client_session_.SendHttp3GoAway(quic::QUIC_PEER_GOING_AWAY, "client goaway");
}

Http::RequestEncoder&
QuicHttpClientConnectionImpl::newStream(Http::ResponseDecoder& response_decoder) {
  EnvoyQuicClientStream* stream =
      quicStreamToEnvoyClientStream(quic_client_session_.CreateOutgoingBidirectionalStream());
  ASSERT(stream != nullptr, "Fail to create QUIC stream.");
  stream->setResponseDecoder(response_decoder);
  if (quic_client_session_.aboveHighWatermark()) {
    stream->runHighWatermarkCallbacks();
  }
  return *stream;
}

void QuicHttpClientConnectionImpl::onUnderlyingConnectionAboveWriteBufferHighWatermark() {
  quic_client_session_.PerformActionOnActiveStreams([](quic::QuicStream* quic_stream) {
    ENVOY_LOG(debug, "runHighWatermarkCallbacks on stream {}", quic_stream->id());
    quicStreamToEnvoyClientStream(quic_stream)->runHighWatermarkCallbacks();
    return true;
  });
}

void QuicHttpClientConnectionImpl::onUnderlyingConnectionBelowWriteBufferLowWatermark() {
  quic_client_session_.PerformActionOnActiveStreams([](quic::QuicStream* quic_stream) {
    ENVOY_LOG(debug, "runLowWatermarkCallbacks on stream {}", quic_stream->id());
    quicStreamToEnvoyClientStream(quic_stream)->runLowWatermarkCallbacks();
    return true;
  });
}

} // namespace Quic
} // namespace Envoy
