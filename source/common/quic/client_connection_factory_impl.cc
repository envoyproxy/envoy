#include "common/quic/client_connection_factory_impl.h"

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

namespace {
// TODO(alyssawilk, danzh2010): This is mutable static info that is required for the QUICHE code.
// This was preexisting but should either be removed or potentially moved inside
// PersistentQuicInfoImpl.
struct StaticInfo {
  quic::QuicConfig quic_config_;
  quic::QuicClientPushPromiseIndex push_promise_index_;

  static StaticInfo& get() { MUTABLE_CONSTRUCT_ON_FIRST_USE(StaticInfo); }
};
} // namespace

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
  auto& static_info = StaticInfo::get();
  auto ret = std::make_unique<EnvoyQuicClientSession>(
      static_info.quic_config_, info_impl->supported_versions_, std::move(connection),
      info_impl->server_id_, info_impl->crypto_config_.get(), &static_info.push_promise_index_,
      dispatcher, 0);
  ret->Initialize();
  return ret;
}

} // namespace Quic
} // namespace Envoy
