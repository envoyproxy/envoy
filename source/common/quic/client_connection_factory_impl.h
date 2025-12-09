#pragma once

#include <memory>

#include "envoy/http/persistent_quic_info.h"
#include "envoy/upstream/upstream.h"

#include "source/common/quic/envoy_quic_alarm_factory.h"
#include "source/common/quic/envoy_quic_client_session.h"
#include "source/common/quic/envoy_quic_connection_helper.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/tls/client_ssl_socket.h"
#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_client_stream.h"

#include "quiche/quic/core/http/quic_connection_migration_manager.h"
#include "quiche/quic/core/quic_utils.h"

namespace Envoy {
namespace Quic {

// Information which can be shared across connections, though not across threads.
// TODO(danzh) considering exposing these QUICHE interfaces via base class virtual methods, so that
// down casting can be avoided while passing around this object.
struct PersistentQuicInfoImpl : public Http::PersistentQuicInfo {
  PersistentQuicInfoImpl(Event::Dispatcher& dispatcher, uint32_t buffer_limit,
                         quic::QuicByteCount max_packet_length = 0);

  EnvoyQuicConnectionHelper conn_helper_;
  EnvoyQuicAlarmFactory alarm_factory_;
  quic::QuicConfig quic_config_;
  // The connection send buffer limits from cluster config.
  const uint32_t buffer_limit_;
  // Hard code with the default crypto stream as there's no pluggable crypto for upstream Envoy.
  EnvoyQuicCryptoClientStreamFactoryImpl crypto_stream_factory_;
  // Override the maximum packet length of connections for tunneling. Use the default length in
  // QUICHE if this is set to 0.
  quic::QuicByteCount max_packet_length_;
  // TODO(danzh): Add a config knob to configure connection migration.
  quic::QuicConnectionMigrationConfig migration_config_;
  QuicClientPacketWriterFactoryPtr writer_factory_;
};

std::unique_ptr<PersistentQuicInfoImpl>
createPersistentQuicInfoForCluster(Event::Dispatcher& dispatcher,
                                   const Upstream::ClusterInfo& cluster);

std::unique_ptr<Network::ClientConnection> createQuicNetworkConnection(
    Http::PersistentQuicInfo& info, std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config,
    const quic::QuicServerId& server_id, Event::Dispatcher& dispatcher,
    Network::Address::InstanceConstSharedPtr server_addr,
    Network::Address::InstanceConstSharedPtr local_addr, QuicStatNames& quic_stat_names,
    OptRef<Http::HttpServerPropertiesCache> rtt_cache, Stats::Scope& scope,
    const Network::ConnectionSocket::OptionsSharedPtr& options,
    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
    quic::ConnectionIdGeneratorInterface& generator,
    Network::UpstreamTransportSocketFactory& transport_socket_factory,
    EnvoyQuicNetworkObserverRegistry* network_observer_registry = nullptr);

} // namespace Quic
} // namespace Envoy
