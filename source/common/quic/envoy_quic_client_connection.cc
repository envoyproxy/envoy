#include "source/common/quic/envoy_quic_client_connection.h"

#include <memory>

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/common/quic/envoy_quic_packet_writer.h"
#include "source/common/quic/envoy_quic_utils.h"

namespace Envoy {
namespace Quic {

EnvoyQuicClientConnection::EnvoyQuicClientConnection(
    const quic::QuicConnectionId& server_connection_id,
    Network::Address::InstanceConstSharedPtr& initial_peer_address,
    quic::QuicConnectionHelperInterface& helper, quic::QuicAlarmFactory& alarm_factory,
    const quic::ParsedQuicVersionVector& supported_versions,
    Network::Address::InstanceConstSharedPtr local_addr, Event::Dispatcher& dispatcher,
    const Network::ConnectionSocket::OptionsSharedPtr& options)
    : EnvoyQuicClientConnection(server_connection_id, helper, alarm_factory, supported_versions,
                                dispatcher,
                                createConnectionSocket(initial_peer_address, local_addr, options)) {
}

EnvoyQuicClientConnection::EnvoyQuicClientConnection(
    const quic::QuicConnectionId& server_connection_id, quic::QuicConnectionHelperInterface& helper,
    quic::QuicAlarmFactory& alarm_factory, const quic::ParsedQuicVersionVector& supported_versions,
    Event::Dispatcher& dispatcher, Network::ConnectionSocketPtr&& connection_socket)
    : EnvoyQuicClientConnection(
          server_connection_id, helper, alarm_factory,
          new EnvoyQuicPacketWriter(
              std::make_unique<Network::UdpDefaultWriter>(connection_socket->ioHandle())),
          true, supported_versions, dispatcher, std::move(connection_socket)) {}

EnvoyQuicClientConnection::EnvoyQuicClientConnection(
    const quic::QuicConnectionId& server_connection_id, quic::QuicConnectionHelperInterface& helper,
    quic::QuicAlarmFactory& alarm_factory, quic::QuicPacketWriter* writer, bool owns_writer,
    const quic::ParsedQuicVersionVector& supported_versions, Event::Dispatcher& dispatcher,
    Network::ConnectionSocketPtr&& connection_socket)
    : quic::QuicConnection(server_connection_id, quic::QuicSocketAddress(),
                           envoyIpAddressToQuicSocketAddress(
                               connection_socket->connectionInfoProvider().remoteAddress()->ip()),
                           &helper, &alarm_factory, writer, owns_writer,
                           quic::Perspective::IS_CLIENT, supported_versions),
      QuicNetworkConnection(std::move(connection_socket)), dispatcher_(dispatcher) {}

void EnvoyQuicClientConnection::processPacket(
    Network::Address::InstanceConstSharedPtr local_address,
    Network::Address::InstanceConstSharedPtr peer_address, Buffer::InstancePtr buffer,
    MonotonicTime receive_time) {
  quic::QuicTime timestamp =
      quic::QuicTime::Zero() +
      quic::QuicTime::Delta::FromMicroseconds(
          std::chrono::duration_cast<std::chrono::microseconds>(receive_time.time_since_epoch())
              .count());
  ASSERT(buffer->getRawSlices().size() == 1);
  Buffer::RawSlice slice = buffer->frontSlice();
  quic::QuicReceivedPacket packet(reinterpret_cast<char*>(slice.mem_), slice.len_, timestamp,
                                  /*owns_buffer=*/false, /*ttl=*/0, /*ttl_valid=*/false,
                                  /*packet_headers=*/nullptr, /*headers_length=*/0,
                                  /*owns_header_buffer*/ false);
  ProcessUdpPacket(envoyIpAddressToQuicSocketAddress(local_address->ip()),
                   envoyIpAddressToQuicSocketAddress(peer_address->ip()), packet);
}

uint64_t EnvoyQuicClientConnection::maxDatagramSize() const {
  // TODO(danzh) make this variable configurable to support jumbo frames.
  return Network::DEFAULT_UDP_MAX_DATAGRAM_SIZE;
}

void EnvoyQuicClientConnection::setUpConnectionSocket(OptRef<PacketsToReadDelegate> delegate) {
  delegate_ = delegate;
  if (connectionSocket()->ioHandle().isOpen()) {
    connectionSocket()->ioHandle().initializeFileEvent(
        dispatcher_, [this](uint32_t events) -> void { onFileEvent(events); },
        Event::PlatformDefaultTriggerType,
        Event::FileReadyType::Read | Event::FileReadyType::Write);

    if (!Network::Socket::applyOptions(connectionSocket()->options(), *connectionSocket(),
                                       envoy::config::core::v3::SocketOption::STATE_LISTENING)) {
      ENVOY_CONN_LOG(error, "Fail to apply listening options", *this);
      connectionSocket()->close();
    }
  }
  if (!connectionSocket()->ioHandle().isOpen()) {
    CloseConnection(quic::QUIC_CONNECTION_CANCELLED, "Fail to set up connection socket.",
                    quic::ConnectionCloseBehavior::SILENT_CLOSE);
  }
}

void EnvoyQuicClientConnection::switchConnectionSocket(
    Network::ConnectionSocketPtr&& connection_socket) {
  auto writer = std::make_unique<EnvoyQuicPacketWriter>(
      std::make_unique<Network::UdpDefaultWriter>(connection_socket->ioHandle()));
  quic::QuicSocketAddress self_address = envoyIpAddressToQuicSocketAddress(
      connection_socket->connectionInfoProvider().localAddress()->ip());
  quic::QuicSocketAddress peer_address = envoyIpAddressToQuicSocketAddress(
      connection_socket->connectionInfoProvider().remoteAddress()->ip());

  // The old socket is closed in this call.
  setConnectionSocket(std::move(connection_socket));
  setUpConnectionSocket(delegate_);
  if (connection_migration_use_new_cid()) {
    MigratePath(self_address, peer_address, writer.release(), true);
  } else {
    SetQuicPacketWriter(writer.release(), true);
  }
}

void EnvoyQuicClientConnection::onFileEvent(uint32_t events) {
  ENVOY_CONN_LOG(trace, "socket event: {}", *this, events);
  ASSERT(events & (Event::FileReadyType::Read | Event::FileReadyType::Write));

  if (events & Event::FileReadyType::Write) {
    OnCanWrite();
  }

  // It's possible for a write event callback to close the connection, in such case ignore read
  // event processing.
  // TODO(mattklein123): Right now QUIC client is hard coded to use GRO because it is probably the
  // right default for QUIC. Determine whether this should be configurable or not.
  if (connected() && (events & Event::FileReadyType::Read)) {
    Api::IoErrorPtr err = Network::Utility::readPacketsFromSocket(
        connectionSocket()->ioHandle(),
        *connectionSocket()->connectionInfoProvider().localAddress(), *this,
        dispatcher_.timeSource(), true, packets_dropped_);
    if (err == nullptr) {
      connectionSocket()->ioHandle().activateFileEvents(Event::FileReadyType::Read);
      return;
    }
    if (err->getErrorCode() != Api::IoError::IoErrorCode::Again) {
      ENVOY_CONN_LOG(error, "recvmsg result {}: {}", *this, static_cast<int>(err->getErrorCode()),
                     err->getErrorDetails());
    }
  }
}

} // namespace Quic
} // namespace Envoy
