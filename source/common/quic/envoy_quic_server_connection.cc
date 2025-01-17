#include "source/common/quic/envoy_quic_server_connection.h"

#include "source/common/network/listen_socket_impl.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_io_handle_wrapper.h"

#include "quiche/quic/core/quic_packets.h"

namespace Envoy {
namespace Quic {

EnvoyQuicServerConnection::EnvoyQuicServerConnection(
    const quic::QuicConnectionId& server_connection_id,
    quic::QuicSocketAddress initial_self_address, quic::QuicSocketAddress initial_peer_address,
    quic::QuicConnectionHelperInterface& helper, quic::QuicAlarmFactory& alarm_factory,
    quic::QuicPacketWriter* writer, bool owns_writer,
    const quic::ParsedQuicVersionVector& supported_versions,
    Network::ConnectionSocketPtr connection_socket, quic::ConnectionIdGeneratorInterface& generator,
    std::unique_ptr<QuicListenerFilterManagerImpl> listener_filter_manager)
    : quic::QuicConnection(server_connection_id, initial_self_address, initial_peer_address,
                           &helper, &alarm_factory, writer, owns_writer,
                           quic::Perspective::IS_SERVER, supported_versions, generator),
      QuicNetworkConnection(std::move(connection_socket)),
      listener_filter_manager_(std::move(listener_filter_manager)) {
#ifndef WIN32
  // Defer sending while processing UDP packets till the end of the current event loop to optimize
  // UDP GSO sendmsg efficiency. But this optimization causes some test failures under Windows,
  // and Windows doesn't support GSO, do not apply this optimization on Windows.
  // TODO(#22976) Figure out if this is needed on Windows.
  set_defer_send_in_response_to_packets(GetQuicFlag(quic_defer_send_in_response));
#endif
}

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

void EnvoyQuicServerConnection::OnCanWrite() {
  quic::QuicConnection::OnCanWrite();
  onWriteEventDone();
}

void EnvoyQuicServerConnection::ProcessUdpPacket(const quic::QuicSocketAddress& self_address,
                                                 const quic::QuicSocketAddress& peer_address,
                                                 const quic::QuicReceivedPacket& packet) {
  if (!first_packet_received_) {
    if (listener_filter_manager_ != nullptr) {
      listener_filter_manager_->onFirstPacketReceived(packet);
    }
    first_packet_received_ = true;
  }

  quic::QuicConnection::ProcessUdpPacket(self_address, peer_address, packet);
};

void EnvoyQuicServerConnection::OnEffectivePeerMigrationValidated(bool is_migration_linkable) {
  quic::QuicConnection::OnEffectivePeerMigrationValidated(is_migration_linkable);
  if (listener_filter_manager_ != nullptr && networkConnection() != nullptr) {
    // This connection might become closed after this call.
    listener_filter_manager_->onPeerAddressChanged(effective_peer_address(), *networkConnection());
  }
}

} // namespace Quic
} // namespace Envoy
