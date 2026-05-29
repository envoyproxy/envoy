#include "source/common/quic/server_codec_impl.h"

#include "envoy/server/overload/load_shed_point.h"
#include "envoy/server/overload/overload_manager.h"

#include "source/common/quic/envoy_quic_server_stream.h"

namespace Envoy {
namespace Quic {

// Converts a QuicStream instance to EnvoyQuicStream instance. The current stream implementation
// inherits from these two interfaces, with the former one providing Quic interface and the latter
// providing Envoy interface.
EnvoyQuicStream* quicStreamToEnvoyStream(quic::QuicStream* stream) {
  return dynamic_cast<EnvoyQuicStream*>(stream);
}

QuicHttpServerConnectionImpl::QuicHttpServerConnectionImpl(
    EnvoyQuicServerSession& quic_session, Http::ServerConnectionCallbacks& callbacks,
    Http::Http3::CodecStats& stats,
    const envoy::config::core::v3::Http3ProtocolOptions& http3_options,
    const uint32_t max_request_headers_kb, const uint32_t max_request_headers_count,
    envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
        headers_with_underscores_action,
    Server::OverloadManager& overload_manager)
    : QuicHttpConnectionImplBase(quic_session, stats), quic_server_session_(quic_session) {
  quic_session.setCodecStats(stats);
  quic_session.setHttp3Options(http3_options);
  quic_session.setHeadersWithUnderscoreAction(headers_with_underscores_action);
  quic_session.setHttpConnectionCallbacks(callbacks);
  quic_session.setMaxIncomingHeadersCount(max_request_headers_count);
  quic_session.set_max_inbound_header_list_size(max_request_headers_kb * 1024u);
  quic_session.setH3GoAwayLoadShedPoints(
      overload_manager.getLoadShedPoint(
          Server::LoadShedPointName::get().H3ServerGoAwayAndCloseOnDispatch),
      overload_manager.getLoadShedPoint(Server::LoadShedPointName::get().H3ServerGoAwayOnDispatch));
}

void QuicHttpServerConnectionImpl::onUnderlyingConnectionAboveWriteBufferHighWatermark() {
  quic_server_session_.PerformActionOnActiveStreams([](quic::QuicStream* quic_stream) {
    ENVOY_LOG(debug, "runHighWatermarkCallbacks on stream {}", quic_stream->id());
    quicStreamToEnvoyStream(quic_stream)->runHighWatermarkCallbacks();
    return true;
  });
}

void QuicHttpServerConnectionImpl::onUnderlyingConnectionBelowWriteBufferLowWatermark() {
  quic_server_session_.PerformActionOnActiveStreams([](quic::QuicStream* quic_stream) {
    ENVOY_LOG(debug, "runLowWatermarkCallbacks on stream {}", quic_stream->id());
    quicStreamToEnvoyStream(quic_stream)->runLowWatermarkCallbacks();
    return true;
  });
}

void QuicHttpServerConnectionImpl::shutdownNotice() {
  quic_server_session_.SendHttp3GoAway(quic::QUIC_PEER_GOING_AWAY, "Server shutdown");
}

void QuicHttpServerConnectionImpl::goAway() {
  quic_server_session_.SendHttp3GoAway(quic::QUIC_PEER_GOING_AWAY, "server shutdown imminent");
}

REGISTER_FACTORY(QuicHttpServerConnectionFactoryImpl, QuicHttpServerConnectionFactory);

} // namespace Quic
} // namespace Envoy
