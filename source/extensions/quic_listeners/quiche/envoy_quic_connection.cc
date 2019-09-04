#include "extensions/quic_listeners/quiche/envoy_quic_connection.h"

#include "common/network/listen_socket_impl.h"

#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"
#include "extensions/quic_listeners/quiche/quic_io_handle_wrapper.h"
#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Quic {

EnvoyQuicConnection::EnvoyQuicConnection(
    const quic::QuicConnectionId& server_connection_id,
    quic::QuicSocketAddress initial_peer_address, quic::QuicConnectionHelperInterface& helper,
    quic::QuicAlarmFactory& alarm_factory, quic::QuicPacketWriter& writer, bool owns_writer,
    quic::Perspective perspective, const quic::ParsedQuicVersionVector& supported_versions,
    Network::ListenerConfig& listener_config, Server::ListenerStats& listener_stats)
    : quic::QuicConnection(server_connection_id, initial_peer_address, &helper, &alarm_factory,
                           &writer, owns_writer, perspective, supported_versions),
      listener_config_(listener_config), listener_stats_(listener_stats) {}

bool EnvoyQuicConnection::OnPacketHeader(const quic::QuicPacketHeader& header) {
  if (quic::QuicConnection::OnPacketHeader(header) && connection_socket_ == nullptr) {
    ASSERT(self_address().IsInitialized());
    // Self address should be initialized by now. It's time to install filters.
    Network::Address::InstanceConstSharedPtr local_addr =
        quicAddressToEnvoyAddressInstance(self_address());

    Network::Address::InstanceConstSharedPtr remote_addr =
        quicAddressToEnvoyAddressInstance(peer_address());
    connection_socket_ = std::make_unique<Network::ConnectionSocketImpl>(
        // Wraps the real IoHandle instance so that if this socket gets closed,
        // the real IoHandle won't be affected.
        std::make_unique<QuicIoHandleWrapper>(listener_config_.socket().ioHandle()),
        std::move(local_addr), std::move(remote_addr));
    connection_socket_->setDetectedTransportProtocol(
        Extensions::TransportSockets::TransportSocketNames::get().Quic);

    filter_chain_ = listener_config_.filterChainManager().findFilterChain(*connection_socket_);
    if (filter_chain_ == nullptr) {
      listener_stats_.no_filter_chain_match_.inc();
      CloseConnection(quic::QUIC_CRYPTO_INTERNAL_ERROR,
                      "closing connection: no matching filter chain found for handshake",
                      quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      return false;
    }
    const bool empty_filter_chain = !listener_config_.filterChainFactory().createNetworkFilterChain(
        *envoy_connection_, filter_chain_->networkFilterFactories());
    if (empty_filter_chain) {
      CloseConnection(quic::QUIC_CRYPTO_INTERNAL_ERROR, "closing connection: filter chain is empty",
                      quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      return false;
    }
  }
  return true;
}

} // namespace Quic
} // namespace Envoy
