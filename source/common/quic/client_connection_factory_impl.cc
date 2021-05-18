#include "common/quic/client_connection_factory_impl.h"

#include "common/quic/envoy_quic_session_cache.h"
#include "common/quic/quic_transport_socket_factory.h"

namespace Envoy {
namespace Quic {

const Envoy::Ssl::ClientContextConfig&
getConfig(Network::TransportSocketFactory& transport_socket_factory) {
  auto* quic_socket_factory =
      dynamic_cast<QuicClientTransportSocketFactory*>(&transport_socket_factory);
  ASSERT(quic_socket_factory != nullptr);
  return quic_socket_factory->clientContextConfig();
}

Envoy::Ssl::ClientContextSharedPtr
getContext(Network::TransportSocketFactory& transport_socket_factory) {
  auto* quic_socket_factory =
      dynamic_cast<QuicClientTransportSocketFactory*>(&transport_socket_factory);
  ASSERT(quic_socket_factory != nullptr);
  ASSERT(quic_socket_factory->sslCtx() != nullptr);
  return quic_socket_factory->sslCtx();
}

PersistentQuicInfoImpl::PersistentQuicInfoImpl(
    Event::Dispatcher& dispatcher, Network::TransportSocketFactory& transport_socket_factory,
    TimeSource& time_source, Network::Address::InstanceConstSharedPtr server_addr)
    : conn_helper_(dispatcher), alarm_factory_(dispatcher, *conn_helper_.GetClock()),
      server_id_{getConfig(transport_socket_factory).serverNameIndication(),
                 static_cast<uint16_t>(server_addr->ip()->port()), false},
      crypto_config_(std::make_unique<quic::QuicCryptoClientConfig>(
          std::make_unique<EnvoyQuicProofVerifier>(getContext(transport_socket_factory)),
          std::make_unique<EnvoyQuicSessionCache>(time_source))) {
  quiche::FlagRegistry::getInstance();
}

std::unique_ptr<Network::ClientConnection>
createQuicNetworkConnection(Http::PersistentQuicInfo& info, Event::Dispatcher& dispatcher,
                            Network::Address::InstanceConstSharedPtr server_addr,
                            Network::Address::InstanceConstSharedPtr local_addr) {
  // This flag fix a QUICHE issue which may crash Envoy during connection close.
  SetQuicReloadableFlag(quic_single_ack_in_packet2, true);
  PersistentQuicInfoImpl* info_impl = reinterpret_cast<PersistentQuicInfoImpl*>(&info);

  auto connection = std::make_unique<EnvoyQuicClientConnection>(
      quic::QuicUtils::CreateRandomConnectionId(), server_addr, info_impl->conn_helper_,
      info_impl->alarm_factory_, quic::ParsedQuicVersionVector{info_impl->supported_versions_[0]},
      local_addr, dispatcher, nullptr);

  ASSERT(!info_impl->supported_versions_.empty());
  // QUICHE client session always use the 1st version to start handshake.
  // TODO(alyssawilk) pass in ClusterInfo::perConnectionBufferLimitBytes() for
  // send_buffer_limit instead of using 0.
  auto ret = std::make_unique<EnvoyQuicClientSession>(
      info_impl->quic_config_, info_impl->supported_versions_, std::move(connection),
      info_impl->server_id_, info_impl->crypto_config_.get(), &info_impl->push_promise_index_,
      dispatcher, /*send_buffer_limit=*/0);
  return ret;
}

} // namespace Quic
} // namespace Envoy
