#include "source/common/quic/quic_network_connectivity_observer_impl.h"

#include "source/common/quic/envoy_quic_client_session.h"

namespace Envoy {
namespace Quic {

QuicNetworkConnectivityObserverImpl::QuicNetworkConnectivityObserverImpl(
    EnvoyQuicClientSession& session)
    : session_(session) {}

void QuicNetworkConnectivityObserverImpl::onNetworkMadeDefault(NetworkHandle network) {
  ENVOY_CONN_LOG(trace, "Network {} has become the default.", session_, network);
  session_.migration_manager().OnNetworkMadeDefault(network);
}

void QuicNetworkConnectivityObserverImpl::onNetworkConnected(NetworkHandle network) {
  ENVOY_CONN_LOG(trace, "Network {} gets connected.", session_, network);
  session_.migration_manager().OnNetworkConnected(network);
}

void QuicNetworkConnectivityObserverImpl::onNetworkDisconnected(NetworkHandle network) {
  ENVOY_CONN_LOG(trace, "Network {} gets disconnected.", session_, network);
  session_.migration_manager().OnNetworkDisconnected(network);
}

} // namespace Quic
} // namespace Envoy
