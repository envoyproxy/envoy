#include "extensions/quic_listeners/quiche/codec_impl.h"

namespace Envoy {
namespace Quic {

void QuicHttpConnectionImplBase::goAway() {
  quic_session_.SendGoAway(quic::QUIC_PEER_GOING_AWAY, "server shutdown imminent");
}

bool QuicHttpConnectionImplBase::wantsToWrite() { return quic_session_.HasDataToWrite(); }

// TODO(danzh): modify QUIC stack to react based on aggregated bytes across all
// streams.
void QuicHttpConnectionImplBase::onUnderlyingConnectionAboveWriteBufferHighWatermark() {
  for (auto& it : quic_session_.stream_map()) {
    if (!it.second->is_static()) {
      dynamic_cast<EnvoyQuicServerStream*>(it.second.get())->runHighWatermarkCallbacks();
    }
  }
}

void QuicHttpConnectionImplBase::onUnderlyingConnectionBelowWriteBufferLowWatermark() {
  for (const auto& it : quic_session_.stream_map()) {
    if (!it.second->is_static()) {
      dynamic_cast<EnvoyQuicServerStream*>(it.second.get())->runLowWatermarkCallbacks();
    }
  }
}

} // namespace Quic
} // namespace Envoy
