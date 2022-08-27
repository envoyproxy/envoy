#include "source/common/quic/envoy_quic_server_connection.h"

#include "source/common/network/listen_socket_impl.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_io_handle_wrapper.h"

namespace Envoy {
namespace Quic {

EnvoyQuicServerConnection::EnvoyQuicServerConnection(
    const quic::QuicConnectionId& server_connection_id,
    quic::QuicSocketAddress initial_self_address, quic::QuicSocketAddress initial_peer_address,
    quic::QuicConnectionHelperInterface& helper, quic::QuicAlarmFactory& alarm_factory,
    quic::QuicPacketWriter* writer, bool owns_writer,
    const quic::ParsedQuicVersionVector& supported_versions,
    Network::ConnectionSocketPtr connection_socket)
    : quic::QuicConnection(server_connection_id, initial_self_address, initial_peer_address,
                           &helper, &alarm_factory, writer, owns_writer,
                           quic::Perspective::IS_SERVER, supported_versions),
      QuicNetworkConnection(std::move(connection_socket)) {}

bool EnvoyQuicServerConnection::OnPacketHeader(const quic::QuicPacketHeader& header) {
  quic::QuicSocketAddress old_self_address = self_address();
  if (!quic::QuicConnection::OnPacketHeader(header)) {
    return false;
  }
  if (old_self_address == self_address()) {
    return true;
  }
  // Update local address if QUICHE has updated the self address.
  ASSERT(self_address().IsInitialized());
  connectionSocket()->connectionInfoProvider().setLocalAddress(
      quicAddressToEnvoyAddressInstance(self_address()));

  return true;
}

std::unique_ptr<quic::QuicSelfIssuedConnectionIdManager>
EnvoyQuicServerConnection::MakeSelfIssuedConnectionIdManager() {
  return std::make_unique<EnvoyQuicSelfIssuedConnectionIdManager>(
      quic::kMinNumOfActiveConnectionIds, connection_id(), clock(), alarm_factory(), this,
      context());
}

quic::QuicConnectionId EnvoyQuicSelfIssuedConnectionIdManager::GenerateNewConnectionId(
    const quic::QuicConnectionId& old_connection_id) const {
  quic::QuicConnectionId new_connection_id =
      quic::QuicSelfIssuedConnectionIdManager::GenerateNewConnectionId(old_connection_id);
  adjustNewConnectionIdForRoutine(new_connection_id, old_connection_id);
  return new_connection_id;
}

} // namespace Quic
} // namespace Envoy
