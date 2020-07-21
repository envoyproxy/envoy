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
    const quic::ParsedQuicVersionVector& supported_versions, Network::Socket& listen_socket)
    : EnvoyQuicConnection(server_connection_id, initial_peer_address, helper, alarm_factory, writer,
                          owns_writer, quic::Perspective::IS_SERVER, supported_versions,
                          std::make_unique<Network::ConnectionSocketImpl>(
                              // Wraps the real IoHandle instance so that if the connection socket
                              // gets closed, the real IoHandle won't be affected.
                              std::make_unique<QuicIoHandleWrapper>(listen_socket.ioHandle()),
                              nullptr, quicAddressToEnvoyAddressInstance(initial_peer_address))) {}

bool EnvoyQuicServerConnection::OnPacketHeader(const quic::QuicPacketHeader& header) {
  if (!EnvoyQuicConnection::OnPacketHeader(header)) {
    return false;
  }
  if (connectionSocket()->localAddress() != nullptr) {
    return true;
  }
  ASSERT(self_address().IsInitialized());
  // Self address should be initialized by now.
  connectionSocket()->setLocalAddress(quicAddressToEnvoyAddressInstance(self_address()));
  connectionSocket()->setDetectedTransportProtocol(
      Extensions::TransportSockets::TransportProtocolNames::get().Quic);
  return true;
}

} // namespace Quic
} // namespace Envoy
