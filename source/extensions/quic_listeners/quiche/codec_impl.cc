#include "extensions/quic_listeners/quiche/codec_impl.h"

#include "extensions/quic_listeners/quiche/envoy_quic_client_stream.h"
#include "extensions/quic_listeners/quiche/envoy_quic_server_stream.h"

namespace Envoy {
namespace Quic {

// Converts a QuicStream instance to EnvoyQuicStream instance. The current stream implementation
// inherits from these two interfaces, with the former one providing Quic interface and the latter
// providing Envoy interface.
EnvoyQuicStream* quicStreamToEnvoyStream(quic::QuicStream* stream) {
  return dynamic_cast<EnvoyQuicStream*>(stream);
}
EnvoyQuicClientStream* quicStreamToEnvoyClientStream(quic::QuicStream* stream) {
  return dynamic_cast<EnvoyQuicClientStream*>(stream);
}

bool QuicHttpConnectionImplBase::wantsToWrite() { return quic_session_.bytesToSend() > 0; }

void QuicHttpConnectionImplBase::runWatermarkCallbacksForEachStream(
    quic::QuicSmallMap<quic::QuicStreamId, std::unique_ptr<quic::QuicStream>, 10>& stream_map,
    bool high_watermark) {
  for (auto& it : stream_map) {
    if (!it.second->is_static()) {
      // Only call watermark callbacks on non QUIC static streams which are
      // crypto stream and Google QUIC headers stream.
      auto stream = quicStreamToEnvoyStream(it.second.get());
      if (high_watermark) {
        ENVOY_LOG(debug, "runHighWatermarkCallbacks on stream {}", it.first);
        stream->runHighWatermarkCallbacks();
      } else {
        ENVOY_LOG(debug, "runLowWatermarkCallbacks on stream {}", it.first);
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
  if (quic::VersionUsesHttp3(quic_server_session_.transport_version())) {
    quic_server_session_.SendHttp3GoAway();
  } else {
    quic_server_session_.SendGoAway(quic::QUIC_PEER_GOING_AWAY, "server shutdown imminent");
  }
}

QuicHttpClientConnectionImpl::QuicHttpClientConnectionImpl(EnvoyQuicClientSession& session,
                                                           Http::ConnectionCallbacks& callbacks)
    : QuicHttpConnectionImplBase(session), quic_client_session_(session) {
  session.setHttpConnectionCallbacks(callbacks);
}

Http::RequestEncoder&
QuicHttpClientConnectionImpl::newStream(Http::ResponseDecoder& response_decoder) {
  EnvoyQuicClientStream* stream =
      quicStreamToEnvoyClientStream(quic_client_session_.CreateOutgoingBidirectionalStream());
  // TODO(danzh) handle stream creation failure gracefully. This can happen when
  // there are already 100 open streams. In such case, caller should hold back
  // the stream creation till an existing stream is closed.
  ASSERT(stream != nullptr, "Fail to create QUIC stream.");
  stream->setResponseDecoder(response_decoder);
  if (quic_client_session_.aboveHighWatermark()) {
    stream->runHighWatermarkCallbacks();
  }
  return *stream;
}

void QuicHttpClientConnectionImpl::onUnderlyingConnectionAboveWriteBufferHighWatermark() {
  runWatermarkCallbacksForEachStream(quic_client_session_.stream_map(), true);
}

void QuicHttpClientConnectionImpl::onUnderlyingConnectionBelowWriteBufferLowWatermark() {
  runWatermarkCallbacksForEachStream(quic_client_session_.stream_map(), false);
}

std::unique_ptr<Http::ClientConnection>
QuicHttpClientConnectionFactoryImpl::createQuicClientConnection(
    Network::Connection& connection, Http::ConnectionCallbacks& callbacks) {
  return std::make_unique<Quic::QuicHttpClientConnectionImpl>(
      dynamic_cast<Quic::EnvoyQuicClientSession&>(connection), callbacks);
}

std::unique_ptr<Http::ServerConnection>
QuicHttpServerConnectionFactoryImpl::createQuicServerConnection(
    Network::Connection& connection, Http::ConnectionCallbacks& callbacks) {
  return std::make_unique<Quic::QuicHttpServerConnectionImpl>(
      dynamic_cast<Quic::EnvoyQuicServerSession&>(connection),
      dynamic_cast<Http::ServerConnectionCallbacks&>(callbacks));
}

REGISTER_FACTORY(QuicHttpClientConnectionFactoryImpl, Http::QuicHttpClientConnectionFactory);
REGISTER_FACTORY(QuicHttpServerConnectionFactoryImpl, Http::QuicHttpServerConnectionFactory);

} // namespace Quic
} // namespace Envoy
