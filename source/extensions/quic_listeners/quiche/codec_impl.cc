#include "extensions/quic_listeners/quiche/codec_impl.h"

namespace Envoy {
namespace Quic {

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

void QuicHttpServerConnectionImpl::goAway() {
  quic_server_session_.SendGoAway(quic::QUIC_PEER_GOING_AWAY, "server shutdown imminent");
}

Http::StreamEncoder&
QuicHttpClientConnectionImpl::newStream(Http::StreamDecoder& response_decoder) {
  auto stream =
      dynamic_cast<EnvoyQuicClientStream*>(quic_session_.CreateOutgoingBidirectionalStream());
  stream->setDecoder(response_decoder);
  return *stream;
}

} // namespace Quic
} // namespace Envoy
