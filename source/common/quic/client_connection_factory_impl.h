#pragma once

#include "common/http/http3/quic_client_connection_factory.h"
#include "common/quic/envoy_quic_alarm_factory.h"
#include "common/quic/envoy_quic_client_session.h"
#include "common/quic/envoy_quic_connection_helper.h"
#include "common/quic/envoy_quic_proof_verifier.h"
#include "common/quic/envoy_quic_utils.h"

#include "extensions/transport_sockets/tls/ssl_socket.h"

#include "quiche/quic/core/http/quic_client_push_promise_index.h"
#include "quiche/quic/core/quic_utils.h"

namespace Envoy {
namespace Quic {

// Information which can be shared across connections, though not across threads.
struct PersistentQuicInfoImpl : public Http::PersistentQuicInfo {
  PersistentQuicInfoImpl(Event::Dispatcher& dispatcher,
                         Network::TransportSocketFactory& transport_socket_factory,
                         Stats::Scope& stats_scope, TimeSource& time_source,
                         Network::Address::InstanceConstSharedPtr server_addr);

  EnvoyQuicConnectionHelper conn_helper_;
  EnvoyQuicAlarmFactory alarm_factory_;
  // server-id and server address can change over the lifetime of Envoy but will be consistent for a
  // given connection pool.
  quic::QuicServerId server_id_;
  quic::ParsedQuicVersionVector supported_versions_{quic::CurrentSupportedVersions()};
  std::unique_ptr<quic::QuicCryptoClientConfig> crypto_config_;
};

std::unique_ptr<Network::ClientConnection>
createQuicNetworkConnection(Http::PersistentQuicInfo& info, Event::Dispatcher& dispatcher,
                            Network::Address::InstanceConstSharedPtr server_addr,
                            Network::Address::InstanceConstSharedPtr local_addr);

} // namespace Quic
} // namespace Envoy
