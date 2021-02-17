#include "extensions/quic_listeners/quiche/client_connection_factory_impl.h"

namespace Envoy {
namespace Quic {

std::unique_ptr<Network::ClientConnection>
QuicClientConnectionFactoryImpl::createQuicNetworkConnection(
    Network::Address::InstanceConstSharedPtr server_addr,
    Network::Address::InstanceConstSharedPtr local_addr,
    Network::TransportSocketFactory& transport_socket_factory, Stats::Scope& stats_scope,
    Event::Dispatcher& dispatcher, TimeSource& time_source) {
  // TODO(#14829): reject the config if a raw buffer socket is configured.
  auto* ssl_socket_factory =
      dynamic_cast<Extensions::TransportSockets::Tls::ClientSslSocketFactory*>(
          &transport_socket_factory);
  ASSERT(ssl_socket_factory != nullptr);

  std::unique_ptr<QuicUpstreamData> upstream_data =
      std::make_unique<QuicUpstreamData>(dispatcher, *ssl_socket_factory->config(), server_addr);
  upstream_data->crypto_config_ = std::make_unique<quic::QuicCryptoClientConfig>(
      std::make_unique<EnvoyQuicProofVerifier>(stats_scope, upstream_data->config_, time_source));

  auto connection = std::make_unique<EnvoyQuicClientConnection>(
      quic::QuicUtils::CreateRandomConnectionId(), server_addr, upstream_data->conn_helper_,
      upstream_data->alarm_factory_,
      quic::ParsedQuicVersionVector{upstream_data->supported_versions_[0]}, local_addr, dispatcher,
      nullptr);
  auto ret = std::make_unique<EnvoyQuicClientSessionWithExtras>(
      quic_config_, upstream_data->supported_versions_, std::move(connection),
      upstream_data->server_id_, upstream_data->crypto_config_.get(), &push_promise_index_,
      dispatcher, 0);
  ret->Initialize();
  ret->quic_upstream_data_ = std::move(upstream_data);
  return ret;
}

REGISTER_FACTORY(QuicClientConnectionFactoryImpl, Http::QuicClientConnectionFactory);

} // namespace Quic
} // namespace Envoy
