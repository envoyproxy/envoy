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

  if (!session_.HasActiveRequestStreams()) {
    // Close the connection if it's idle.
    session_.CloseConnectionWithDetails(quic::QUIC_CONNECTION_MIGRATION_NO_MIGRATABLE_STREAMS,
                                        "net_error");
    return;
  }

  // Drain this session as if it received a GoAway.
  session_.notifyServerGoAway();
}

} // namespace Quic
} // namespace Envoy
