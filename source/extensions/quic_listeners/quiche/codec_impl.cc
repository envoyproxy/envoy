#include "extensions/quic_listeners/quiche/codec_impl.h"

namespace Envoy {
namespace Quic {

bool QuicHttpConnectionImplBase::wantsToWrite() { return quic_session_.HasDataToWrite(); }

// TODO(danzh): modify QUIC stack to react based on aggregated bytes across all
// the streams. And call StreamCallbackHelper::runHighWatermarkCallbacks() for each stream.
void QuicHttpConnectionImplBase::onUnderlyingConnectionAboveWriteBufferHighWatermark() {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

void QuicHttpConnectionImplBase::onUnderlyingConnectionBelowWriteBufferLowWatermark() {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

void QuicHttpServerConnectionImpl::goAway() {
  quic_server_session_.SendGoAway(quic::QUIC_PEER_GOING_AWAY, "server shutdown imminent");
}

} // namespace Quic
} // namespace Envoy
