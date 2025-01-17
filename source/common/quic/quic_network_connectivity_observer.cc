#include "source/common/quic/quic_network_connectivity_observer.h"

#include "source/common/quic/envoy_quic_client_session.h"

namespace Envoy {
namespace Quic {

QuicNetworkConnectivityObserver::QuicNetworkConnectivityObserver(EnvoyQuicClientSession& session)
    : session_(session) {}

} // namespace Quic
} // namespace Envoy
