#pragma once

#include "common/http/http3/quic_client_connection_factory.h"
#include "common/http/http3/well_known_names.h"

#include "extensions/quic_listeners/quiche/envoy_quic_alarm_factory.h"
#include "extensions/quic_listeners/quiche/envoy_quic_client_session.h"
#include "extensions/quic_listeners/quiche/envoy_quic_connection_helper.h"
#include "extensions/quic_listeners/quiche/envoy_quic_proof_verifier.h"
#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"
#include "extensions/transport_sockets/tls/ssl_socket.h"

#include "quiche/quic/core/http/quic_client_push_promise_index.h"
#include "quiche/quic/core/quic_utils.h"

namespace Envoy {
namespace Quic {

// TODO(#14829) we should avoid creating this per-connection.
// Figure out what goes in per-cluster data, and what is per-connection and clean up.
struct QuicUpstreamData {
  QuicUpstreamData(Event::Dispatcher& dispatcher, const Envoy::Ssl::ClientContextConfig& config,
                   Network::Address::InstanceConstSharedPtr server_addr)
      : conn_helper_(dispatcher), alarm_factory_(dispatcher, *conn_helper_.GetClock()),
        server_id_{config.serverNameIndication(), static_cast<uint16_t>(server_addr->ip()->port()),
                   false} {}

  EnvoyQuicConnectionHelper conn_helper_;
  EnvoyQuicAlarmFactory alarm_factory_;
  quic::QuicServerId server_id_;
  std::unique_ptr<quic::QuicCryptoClientConfig> crypto_config_;
  quic::ParsedQuicVersionVector supported_versions_{quic::CurrentSupportedVersions()};
};

class EnvoyQuicClientSessionWithExtras : public EnvoyQuicClientSession {
public:
  using EnvoyQuicClientSession::EnvoyQuicClientSession;

  std::unique_ptr<QuicUpstreamData> quic_upstream_data_;
};

// A factory to create EnvoyQuicClientConnection instance for QUIC
class QuicClientConnectionFactoryImpl : public Http::QuicClientConnectionFactory {
public:
  std::unique_ptr<Network::ClientConnection>
  createQuicNetworkConnection(Network::Address::InstanceConstSharedPtr server_addr,
                              Network::Address::InstanceConstSharedPtr local_addr,
                              Network::TransportSocketFactory& transport_socket_factory,
                              Stats::Scope& stats_scope, Event::Dispatcher& dispatcher,
                              TimeSource& time_source) override;

  std::string name() const override { return Http::QuicCodecNames::get().Quiche; }

  quic::QuicConfig quic_config_;
  quic::QuicClientPushPromiseIndex push_promise_index_;
};

DECLARE_FACTORY(QuicClientConnectionFactoryImpl);

} // namespace Quic
} // namespace Envoy
