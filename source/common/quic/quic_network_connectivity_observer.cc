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

  if (!session_.HasActiveRequestStreams() && session_.connection()->IsHandshakeComplete()) {
    // Close the connection if it's idle and has finished handshake. Connections
    // which are still doing handshake will be drained instead to avoid propagation of upstream
    // pool failure to downstream.
    session_.CloseConnectionWithDetails(quic::QUIC_CONNECTION_MIGRATION_NO_MIGRATABLE_STREAMS,
                                        "net_error");
    return;
  }

  // Drain this session as if it received a GoAway.
  ENVOY_CONN_LOG(
      trace,
      "The default network changed. Drain the connection with in-flight requests or handshake.",
      session_);
  session_.notifyNetworkChange();
}

} // namespace Quic
} // namespace Envoy
