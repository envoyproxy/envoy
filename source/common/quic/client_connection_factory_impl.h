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
                         TimeSource& time_source,
                         Network::Address::InstanceConstSharedPtr server_addr);

  // Returns the most recent crypto config from transport_socket_factory_;
  std::shared_ptr<quic::QuicCryptoClientConfig> cryptoConfig();

  EnvoyQuicConnectionHelper conn_helper_;
  EnvoyQuicAlarmFactory alarm_factory_;
  // server-id can change over the lifetime of Envoy but will be consistent for a
  // given connection pool.
  quic::QuicServerId server_id_;
  // Latch the transport socket factory, to get the latest crypto config and the
  // time source to create it.
  Network::TransportSocketFactory& transport_socket_factory_;
  TimeSource& time_source_;
  // Latch the latest crypto config, to determine if it has updated since last
  // checked.
  Envoy::Ssl::ClientContextSharedPtr client_context_;
  // If client context changes, client config will be updated as well.
  std::shared_ptr<quic::QuicCryptoClientConfig> client_config_;
  const quic::ParsedQuicVersionVector supported_versions_{quic::CurrentSupportedVersions()};
  // TODO(alyssawilk) actually set this up properly.
  quic::QuicConfig quic_config_;
  // This arguably should not be shared across connections but as Envoy doesn't
  // support push promise it's really moot point.
  quic::QuicClientPushPromiseIndex push_promise_index_;
};

std::unique_ptr<Network::ClientConnection>
createQuicNetworkConnection(Http::PersistentQuicInfo& info, Event::Dispatcher& dispatcher,
                            Network::Address::InstanceConstSharedPtr server_addr,
                            Network::Address::InstanceConstSharedPtr local_addr);

} // namespace Quic
} // namespace Envoy
