#include "extensions/quic_listeners/quiche/codec_impl.h"

#include "extensions/quic_listeners/quiche/envoy_quic_client_stream.h"
#include "extensions/quic_listeners/quiche/envoy_quic_server_stream.h"

namespace Envoy {
namespace Quic {

bool QuicHttpConnectionImplBase::wantsToWrite() { return quic_session_.HasDataToWrite(); }

QuicHttpServerConnectionImpl::QuicHttpServerConnectionImpl(
    EnvoyQuicServerSession& quic_session, Http::ServerConnectionCallbacks& callbacks)
    : QuicHttpConnectionImplBase(quic_session), quic_server_session_(quic_session) {
  quic_session.setHttpConnectionCallbacks(callbacks);
}

// TODO(danzh): modify QUIC stack to react based on aggregated bytes across all
// streams.
void QuicHttpServerConnectionImpl::onUnderlyingConnectionAboveWriteBufferHighWatermark() {
  for (auto& it : quic_server_session_.stream_map()) {
    if (!it.second->is_static()) {
      dynamic_cast<EnvoyQuicServerStream*>(it.second.get())->runHighWatermarkCallbacks();
    }
  }
}

void QuicHttpServerConnectionImpl::onUnderlyingConnectionBelowWriteBufferLowWatermark() {
  for (const auto& it : quic_server_session_.stream_map()) {
    if (!it.second->is_static()) {
      dynamic_cast<EnvoyQuicServerStream*>(it.second.get())->runLowWatermarkCallbacks();
    }
  }
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
  stream->setDecoder(response_decoder);
  return *stream;
}

// TODO(danzh): modify QUIC stack to react based on aggregated bytes across all
// streams.
void QuicHttpClientConnectionImpl::onUnderlyingConnectionAboveWriteBufferHighWatermark() {
  for (auto& it : quic_client_session_.stream_map()) {
    if (!it.second->is_static()) {
      dynamic_cast<EnvoyQuicClientStream*>(it.second.get())->runHighWatermarkCallbacks();
    }
  }
}

void QuicHttpClientConnectionImpl::onUnderlyingConnectionBelowWriteBufferLowWatermark() {
  for (const auto& it : quic_client_session_.stream_map()) {
    if (!it.second->is_static()) {
      dynamic_cast<EnvoyQuicClientStream*>(it.second.get())->runLowWatermarkCallbacks();
    }
  }
}

} // namespace Quic
} // namespace Envoy
