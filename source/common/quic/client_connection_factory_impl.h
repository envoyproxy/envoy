#pragma once

#include "source/common/http/http3/quic_client_connection_factory.h"
#include "source/common/quic/envoy_quic_alarm_factory.h"
#include "source/common/quic/envoy_quic_client_session.h"
#include "source/common/quic/envoy_quic_connection_helper.h"
#include "source/common/quic/envoy_quic_proof_verifier.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_client_stream.h"
#include "source/extensions/transport_sockets/tls/ssl_socket.h"

#include "quiche/quic/core/http/quic_client_push_promise_index.h"
#include "quiche/quic/core/quic_utils.h"

namespace Envoy {
namespace Quic {

// Information which can be shared across connections, though not across threads.
struct PersistentQuicInfoImpl : public Http::PersistentQuicInfo {
  PersistentQuicInfoImpl(Event::Dispatcher& dispatcher,
                         Network::TransportSocketFactory& transport_socket_factory,
                         TimeSource& time_source,
                         Network::Address::InstanceConstSharedPtr server_addr,
                         const quic::QuicConfig& quic_config, uint32_t buffer_limit);

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
  quic::QuicConfig quic_config_;
  // The cluster buffer limits.
  const uint32_t buffer_limit_;
  // This arguably should not be shared across connections but as Envoy doesn't
  // support push promise it's really moot point.
  quic::QuicClientPushPromiseIndex push_promise_index_;
  // Hard code with the default crypto stream as there's no pluggable crypto for upstream Envoy.
  EnvoyQuicCryptoClientStreamFactoryImpl crypto_stream_factory_;
};

std::unique_ptr<Network::ClientConnection>
createQuicNetworkConnection(Http::PersistentQuicInfo& info, Event::Dispatcher& dispatcher,
                            Network::Address::InstanceConstSharedPtr server_addr,
                            Network::Address::InstanceConstSharedPtr local_addr,
                            QuicStatNames& quic_stat_names, Stats::Scope& scope);

} // namespace Quic
} // namespace Envoy
