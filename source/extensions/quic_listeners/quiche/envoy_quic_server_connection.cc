#include "extensions/quic_listeners/quiche/envoy_quic_server_connection.h"

#include "common/network/listen_socket_impl.h"

#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"
#include "extensions/quic_listeners/quiche/quic_io_handle_wrapper.h"
#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Quic {

EnvoyQuicServerConnection::EnvoyQuicServerConnection(
    const quic::QuicConnectionId& server_connection_id,
    quic::QuicSocketAddress initial_peer_address, quic::QuicConnectionHelperInterface& helper,
    quic::QuicAlarmFactory& alarm_factory, quic::QuicPacketWriter* writer, bool owns_writer,
    const quic::ParsedQuicVersionVector& supported_versions,
    Network::ListenerConfig& listener_config, Server::ListenerStats& listener_stats,
    Network::Socket& listen_socket)
    : EnvoyQuicConnection(server_connection_id, initial_peer_address, helper, alarm_factory, writer,
                          owns_writer, quic::Perspective::IS_SERVER, supported_versions,
                          std::make_unique<Network::ConnectionSocketImpl>(
                              // Wraps the real IoHandle instance so that if the connection socket
                              // gets closed, the real IoHandle won't be affected.
                              std::make_unique<QuicIoHandleWrapper>(listen_socket.ioHandle()),
                              nullptr, quicAddressToEnvoyAddressInstance(initial_peer_address))),
      listener_config_(listener_config), listener_stats_(listener_stats) {}

bool EnvoyQuicServerConnection::OnPacketHeader(const quic::QuicPacketHeader& header) {
  if (!EnvoyQuicConnection::OnPacketHeader(header)) {
    return false;
  }
  if (connectionSocket()->localAddress() != nullptr) {
    return true;
  }
  ASSERT(self_address().IsInitialized());
  // Self address should be initialized by now. It's time to install filters.
  connectionSocket()->setLocalAddress(quicAddressToEnvoyAddressInstance(self_address()));
  connectionSocket()->setDetectedTransportProtocol(
      Extensions::TransportSockets::TransportProtocolNames::get().Quic);
  ASSERT(filter_chain_ == nullptr);
  filter_chain_ = listener_config_.filterChainManager().findFilterChain(*connectionSocket());
  if (filter_chain_ == nullptr) {
    listener_stats_.no_filter_chain_match_.inc();
    CloseConnection(quic::QUIC_CRYPTO_INTERNAL_ERROR,
                    "closing connection: no matching filter chain found for handshake",
                    quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }
  const bool empty_filter_chain = !listener_config_.filterChainFactory().createNetworkFilterChain(
      envoyConnection(), filter_chain_->networkFilterFactories());
  if (empty_filter_chain) {
    // TODO(danzh) check empty filter chain at config load time instead of here.
    CloseConnection(quic::QUIC_CRYPTO_INTERNAL_ERROR, "closing connection: filter chain is empty",
                    quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }
  return true;
}

} // namespace Quic
} // namespace Envoy
