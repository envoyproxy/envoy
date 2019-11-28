#include "extensions/quic_listeners/quiche/codec_impl.h"

#include "extensions/quic_listeners/quiche/envoy_quic_server_stream.h"

namespace Envoy {
namespace Quic {

bool QuicHttpConnectionImplBase::wantsToWrite() { return quic_session_.HasDataToWrite(); }

QuicHttpServerConnectionImpl::QuicHttpServerConnectionImpl(
    EnvoyQuicServerSession& quic_session, Http::ServerConnectionCallbacks& callbacks)
    : QuicHttpConnectionImplBase(quic_session), quic_server_session_(quic_session) {
  quic_session.setHttpConnectionCallbacks(callbacks);
}

void QuicHttpServerConnectionImpl::onUnderlyingConnectionAboveWriteBufferHighWatermark() {
  for (auto& it : quic_server_session_.stream_map()) {
    if (!it.second->is_static()) {
      // Only call watermark callbacks on non QUIC static streams which are
      // crypto stream and Google QUIC headers stream.
      ENVOY_LOG(debug, "runHighWatermarkCallbacks on stream {}", it.first);
      dynamic_cast<EnvoyQuicServerStream*>(it.second.get())->runHighWatermarkCallbacks();
    }
  }
}

void QuicHttpServerConnectionImpl::onUnderlyingConnectionBelowWriteBufferLowWatermark() {
  for (const auto& it : quic_server_session_.stream_map()) {
    if (!it.second->is_static()) {
      ENVOY_LOG(debug, "runLowWatermarkCallbacks on stream {}", it.first);
      dynamic_cast<EnvoyQuicServerStream*>(it.second.get())->runLowWatermarkCallbacks();
    }
  }
}

void QuicHttpServerConnectionImpl::goAway() {
  quic_server_session_.SendGoAway(quic::QUIC_PEER_GOING_AWAY, "server shutdown imminent");
}

} // namespace Quic
} // namespace Envoy
