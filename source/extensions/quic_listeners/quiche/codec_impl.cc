#include "extensions/quic_listeners/quiche/codec_impl.h"

#include "extensions/quic_listeners/quiche/envoy_quic_client_stream.h"
#include "extensions/quic_listeners/quiche/envoy_quic_server_stream.h"

namespace Envoy {
namespace Quic {

bool QuicHttpConnectionImplBase::wantsToWrite() { return quic_session_.HasDataToWrite(); }

void QuicHttpConnectionImplBase::runWatermarkCallbacksForEachStream(
    quic::QuicSmallMap<quic::QuicStreamId, std::unique_ptr<quic::QuicStream>, 10>& stream_map,
    bool high_watermark) {
  for (auto& it : stream_map) {
    if (!it.second->is_static()) {
      auto stream = dynamic_cast<EnvoyQuicStream*>(it.second.get());
      if (high_watermark) {
        stream->runHighWatermarkCallbacks();
      } else {
        stream->runLowWatermarkCallbacks();
      }
    }
  }
}

QuicHttpServerConnectionImpl::QuicHttpServerConnectionImpl(
    EnvoyQuicServerSession& quic_session, Http::ServerConnectionCallbacks& callbacks)
    : QuicHttpConnectionImplBase(quic_session), quic_server_session_(quic_session) {
  quic_session.setHttpConnectionCallbacks(callbacks);
}

void QuicHttpServerConnectionImpl::onUnderlyingConnectionAboveWriteBufferHighWatermark() {
  runWatermarkCallbacksForEachStream(quic_server_session_.stream_map(), true);
}

void QuicHttpServerConnectionImpl::onUnderlyingConnectionBelowWriteBufferLowWatermark() {
  runWatermarkCallbacksForEachStream(quic_server_session_.stream_map(), false);
}

void QuicHttpServerConnectionImpl::goAway() {
  quic_server_session_.SendGoAway(quic::QUIC_PEER_GOING_AWAY, "server shutdown imminent");
}

QuicHttpClientConnectionImpl::QuicHttpClientConnectionImpl(EnvoyQuicClientSession& session,
                                                           Http::ConnectionCallbacks& callbacks)
    : QuicHttpConnectionImplBase(session), quic_client_session_(session) {
  session.setHttpConnectionCallbacks(callbacks);
}

Http::StreamEncoder&
QuicHttpClientConnectionImpl::newStream(Http::StreamDecoder& response_decoder) {
  auto stream = dynamic_cast<EnvoyQuicClientStream*>(
      quic_client_session_.CreateOutgoingBidirectionalStream());
  ASSERT(stream != nullptr, "Fail to create QUIC stream.");
  stream->setDecoder(response_decoder);
  return *stream;
}

void QuicHttpClientConnectionImpl::onUnderlyingConnectionAboveWriteBufferHighWatermark() {
  runWatermarkCallbacksForEachStream(quic_client_session_.stream_map(), true);
}

void QuicHttpClientConnectionImpl::onUnderlyingConnectionBelowWriteBufferLowWatermark() {
  runWatermarkCallbacksForEachStream(quic_client_session_.stream_map(), false);
}

Http::Connection*
QuicHttpClientConnectionFactory::createQuicHttpConnection(Network::Connection& connection,
                                                          Http::ConnectionCallbacks& callbacks) {
  return new Quic::QuicHttpClientConnectionImpl(
      dynamic_cast<Quic::EnvoyQuicClientSession&>(connection), callbacks);
}

Http::Connection*
QuicHttpServerConnectionFactory::createQuicHttpConnection(Network::Connection& connection,
                                                          Http::ConnectionCallbacks& callbacks) {
  return new Quic::QuicHttpServerConnectionImpl(
      dynamic_cast<Quic::EnvoyQuicServerSession&>(connection),
      dynamic_cast<Http::ServerConnectionCallbacks&>(callbacks));
}

REGISTER_FACTORY(QuicHttpClientConnectionFactory, Http::QuicHttpConnectionFactory);
REGISTER_FACTORY(QuicHttpServerConnectionFactory, Http::QuicHttpConnectionFactory);

} // namespace Quic
} // namespace Envoy
