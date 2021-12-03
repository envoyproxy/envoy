#include "source/common/quic/client_connection_factory_impl.h"

#include "source/common/quic/quic_transport_socket_factory.h"

#include "quiche/quic/core/crypto/quic_client_session_cache.h"

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
  return quic_socket_factory->sslCtx();
}

std::shared_ptr<quic::QuicCryptoClientConfig> PersistentQuicInfoImpl::cryptoConfig() {
  auto context = getContext(transport_socket_factory_);
  // If the secrets haven't been loaded, there is no crypto config.
  if (context == nullptr) {
    return nullptr;
  }

  // If the secret has been updated, update the proof source.
  if (context.get() != client_context_.get()) {
    client_context_ = context;
    client_config_ = std::make_shared<quic::QuicCryptoClientConfig>(
        std::make_unique<EnvoyQuicProofVerifier>(getContext(transport_socket_factory_)),
        std::make_unique<quic::QuicClientSessionCache>());
  }
  // Return the latest client config.
  return client_config_;
}

PersistentQuicInfoImpl::PersistentQuicInfoImpl(
    Event::Dispatcher& dispatcher, Network::TransportSocketFactory& transport_socket_factory,
    TimeSource& time_source, Network::Address::InstanceConstSharedPtr server_addr,
    const quic::QuicConfig& quic_config, uint32_t buffer_limit)
    : conn_helper_(dispatcher), alarm_factory_(dispatcher, *conn_helper_.GetClock()),
      server_id_{getConfig(transport_socket_factory).serverNameIndication(),
                 static_cast<uint16_t>(server_addr->ip()->port()), false},
      transport_socket_factory_(transport_socket_factory), time_source_(time_source),
      quic_config_(quic_config), buffer_limit_(buffer_limit) {
  quiche::FlagRegistry::getInstance();
}

std::unique_ptr<Network::ClientConnection>
createQuicNetworkConnection(Http::PersistentQuicInfo& info, Event::Dispatcher& dispatcher,
                            Network::Address::InstanceConstSharedPtr server_addr,
                            Network::Address::InstanceConstSharedPtr local_addr,
                            QuicStatNames& quic_stat_names, Stats::Scope& scope) {
  ASSERT(GetQuicReloadableFlag(quic_single_ack_in_packet2));
  PersistentQuicInfoImpl* info_impl = reinterpret_cast<PersistentQuicInfoImpl*>(&info);
  auto config = info_impl->cryptoConfig();
  if (config == nullptr) {
    return nullptr; // no secrets available yet.
  }
  quic::ParsedQuicVersionVector quic_versions = quic::CurrentSupportedHttp3Versions();
  ASSERT(!quic_versions.empty());
  auto connection = std::make_unique<EnvoyQuicClientConnection>(
      quic::QuicUtils::CreateRandomConnectionId(), server_addr, info_impl->conn_helper_,
      info_impl->alarm_factory_, quic_versions, local_addr, dispatcher, nullptr);

  // QUICHE client session always use the 1st version to start handshake.
  auto ret = std::make_unique<EnvoyQuicClientSession>(
      info_impl->quic_config_, quic_versions, std::move(connection), info_impl->server_id_,
      std::move(config), &info_impl->push_promise_index_, dispatcher, info_impl->buffer_limit_,
      info_impl->crypto_stream_factory_, quic_stat_names, scope);
  return ret;
}

} // namespace Quic
} // namespace Envoy
