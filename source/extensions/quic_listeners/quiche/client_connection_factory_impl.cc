#include "extensions/quic_listeners/quiche/client_connection_factory_impl.h"

#include "extensions/quic_listeners/quiche/quic_transport_socket_factory.h"

namespace Envoy {
namespace Quic {

const Envoy::Ssl::ClientContextConfig&
getConfig(Network::TransportSocketFactory& transport_socket_factory) {
  auto* quic_socket_factory =
      dynamic_cast<QuicClientTransportSocketFactory*>(&transport_socket_factory);
  ASSERT(quic_socket_factory != nullptr);
  return quic_socket_factory->clientContextConfig();
}

PersistentQuicInfoImpl::PersistentQuicInfoImpl(
    Event::Dispatcher& dispatcher, Network::TransportSocketFactory& transport_socket_factory,
    Stats::Scope& stats_scope, TimeSource& time_source,
    Network::Address::InstanceConstSharedPtr server_addr)
    : conn_helper_(dispatcher), alarm_factory_(dispatcher, *conn_helper_.GetClock()),
      server_id_{getConfig(transport_socket_factory).serverNameIndication(),
                 static_cast<uint16_t>(server_addr->ip()->port()), false},
      crypto_config_(
          std::make_unique<quic::QuicCryptoClientConfig>(std::make_unique<EnvoyQuicProofVerifier>(
              stats_scope, getConfig(transport_socket_factory), time_source))) {}

std::unique_ptr<Network::ClientConnection>
QuicClientConnectionFactoryImpl::createQuicNetworkConnection(
    Http::PersistentQuicInfo& info, Event::Dispatcher& dispatcher,
    Network::Address::InstanceConstSharedPtr server_addr,
    Network::Address::InstanceConstSharedPtr local_addr) {
  PersistentQuicInfoImpl* info_impl = reinterpret_cast<PersistentQuicInfoImpl*>(&info);

  auto connection = std::make_unique<EnvoyQuicClientConnection>(
      quic::QuicUtils::CreateRandomConnectionId(), server_addr, info_impl->conn_helper_,
      info_impl->alarm_factory_, quic::ParsedQuicVersionVector{info_impl->supported_versions_[0]},
      local_addr, dispatcher, nullptr);
  auto ret = std::make_unique<EnvoyQuicClientSession>(
      quic_config_, info_impl->supported_versions_, std::move(connection), info_impl->server_id_,
      info_impl->crypto_config_.get(), &push_promise_index_, dispatcher, 0);
  ret->Initialize();
  return ret;
}

REGISTER_FACTORY(QuicClientConnectionFactoryImpl, Http::QuicClientConnectionFactory);

} // namespace Quic
} // namespace Envoy
