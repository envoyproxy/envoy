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

QuicHttpServerConnectionImpl::QuicHttpServerConnectionImpl(
    EnvoyQuicServerSession& quic_session, Http::ServerConnectionCallbacks& callbacks)
    : QuicHttpConnectionImplBase(quic_session), quic_server_session_(quic_session) {
  quic_session.setHttpConnectionCallbacks(callbacks);
}

void QuicHttpServerConnectionImpl::onUnderlyingConnectionAboveWriteBufferHighWatermark() {
  quic_server_session_.PerformActionOnActiveStreams([](quic::QuicStream* quic_stream) {
    auto stream = quicStreamToEnvoyStream(quic_stream);
    ENVOY_LOG(debug, "runHighWatermarkCallbacks on stream {}", quic_stream->id());
    stream->runHighWatermarkCallbacks();
    return true;
  });
}

void QuicHttpServerConnectionImpl::onUnderlyingConnectionBelowWriteBufferLowWatermark() {
  quic_server_session_.PerformActionOnActiveStreams([](quic::QuicStream* quic_stream) {
    auto stream = quicStreamToEnvoyStream(quic_stream);
    ENVOY_LOG(debug, "runLowWatermarkCallbacks on stream {}", quic_stream->id());
    stream->runLowWatermarkCallbacks();
    return true;
  });
}

void QuicHttpServerConnectionImpl::shutdownNotice() {
  if (quic::VersionUsesHttp3(quic_server_session_.transport_version())) {
    quic_server_session_.SendHttp3Shutdown();
  } else {
    ENVOY_CONN_LOG(debug, "Shutdown notice is not propagated to QUIC.", quic_server_session_);
  }
}

void QuicHttpServerConnectionImpl::goAway() {
  if (quic::VersionUsesHttp3(quic_server_session_.transport_version())) {
    quic_server_session_.SendHttp3GoAway(quic::QUIC_PEER_GOING_AWAY, "server shutdown imminent");
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
  quic_client_session_.PerformActionOnActiveStreams([](quic::QuicStream* quic_stream) {
    auto stream = quicStreamToEnvoyStream(quic_stream);
    ENVOY_LOG(debug, "runHighWatermarkCallbacks on stream {}", quic_stream->id());
    stream->runHighWatermarkCallbacks();
    return true;
  });
}

void QuicHttpClientConnectionImpl::onUnderlyingConnectionBelowWriteBufferLowWatermark() {
  quic_client_session_.PerformActionOnActiveStreams([](quic::QuicStream* quic_stream) {
    auto stream = quicStreamToEnvoyStream(quic_stream);
    ENVOY_LOG(debug, "runLowWatermarkCallbacks on stream {}", quic_stream->id());
    stream->runLowWatermarkCallbacks();
    return true;
  });
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
