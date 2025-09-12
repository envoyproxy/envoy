#include "source/common/quic/quic_network_connectivity_observer_impl.h"

#include "source/common/quic/envoy_quic_client_session.h"

namespace Envoy {
namespace Quic {

QuicNetworkConnectivityObserverImpl::QuicNetworkConnectivityObserverImpl(
    EnvoyQuicClientSession& session)
    : session_(session) {}

void QuicNetworkConnectivityObserverImpl::onNetworkChanged(NetworkHandle /*network*/) {
  if (!session_.connection()->connected()) {
    return;
  }

  session_.onNetworkMadeDefault();
}

} // namespace Quic
} // namespace Envoy
