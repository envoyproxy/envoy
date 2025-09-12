#include "source/common/quic/quic_network_connectivity_observer_impl.h"

#include "source/common/quic/envoy_quic_client_session.h"

namespace Envoy {
namespace Quic {

QuicNetworkConnectivityObserverImpl::QuicNetworkConnectivityObserverImpl(
    EnvoyQuicClientSession& session)
    : session_(session) {}

void QuicNetworkConnectivityObserverImpl::onNetworkMadeDefault(NetworkHandle /*network*/) {
  if (!session_.connection()->connected()) {
    return;
  }

  session_.onNetworkMadeDefault();
}

void QuicNetworkConnectivityObserverImpl::onNetworkConnected(NetworkHandle network) {
  if (!session_.connection()->connected()) {
    return;
  }
  session_.onNetworkConnected(network);
}

} // namespace Quic
} // namespace Envoy
