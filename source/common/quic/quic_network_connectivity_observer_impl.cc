#include "source/common/quic/quic_network_connectivity_observer_impl.h"

#include "source/common/quic/envoy_quic_client_session.h"

namespace Envoy {
namespace Quic {

QuicNetworkConnectivityObserverImpl::QuicNetworkConnectivityObserverImpl(
    EnvoyQuicClientSession& session)
    : session_(session) {}

void QuicNetworkConnectivityObserverImpl::onNetworkMadeDefault(NetworkHandle /*network*/) {
  // TODO(danzh) propagate `network` to quic session.
  (void)session_;
}

void QuicNetworkConnectivityObserverImpl::onNetworkConnected(NetworkHandle /*network*/) {
  (void)session_;
}

void QuicNetworkConnectivityObserverImpl::onNetworkDisconnected(NetworkHandle /*network*/) {
  (void)session_;
}

} // namespace Quic
} // namespace Envoy
