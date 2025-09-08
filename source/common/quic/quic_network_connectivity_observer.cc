#include "source/common/quic/quic_network_connectivity_observer.h"

#include "source/common/quic/envoy_quic_client_session.h"

namespace Envoy {
namespace Quic {

QuicNetworkConnectivityObserver::QuicNetworkConnectivityObserver(EnvoyQuicClientSession& session)
    : session_(session) {}

void QuicNetworkConnectivityObserver::onNetworkChanged() {
  if (!session_.connection()->connected()) {
    return;
  }

  session_.onNetworkMadeDefault();
}

} // namespace Quic
} // namespace Envoy
