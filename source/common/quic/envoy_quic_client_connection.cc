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

void EnvoyQuicClientConnection::setUpConnectionSocket(Network::ConnectionSocketPtr&& connection_socket, OptRef<PacketsToReadDelegate> delegate) {
  delegate_ = delegate;
  if (connection_socket->ioHandle().isOpen()) {
    connection_socket->ioHandle().initializeFileEvent(
        dispatcher_, [this](uint32_t events) -> void { onFileEvent(events); },
        Event::PlatformDefaultTriggerType,
        Event::FileReadyType::Read | Event::FileReadyType::Write);

    if (!Network::Socket::applyOptions(connection_socket->options(), *connection_socket,
                                       envoy::config::core::v3::SocketOption::STATE_LISTENING)) {
      ENVOY_CONN_LOG(error, "Fail to apply listening options", *this);
      connection_socket->close();
    }
  }
  if (!connection_socket->ioHandle().isOpen()) {
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
  setUpConnectionSocket(connectionSocket(), delegate_);
  if (connection_migration_use_new_cid()) {
    MigratePath(self_address, peer_address, writer.release(), true);
  } else {
    SetQuicPacketWriter(writer.release(), true);
  }
}

void EnvoyQuicClientConnection::OnPathDegradingDetected() {
  QuicConnection::OnPathDegradingDetected();
  MaybeMigratePort();
}

void EnvoyQuicClientConnection::MaybeMigratePort() {
  if (!IsHandshakeConfirmed() || !connection_migration_use_new_cid() || HasPendingPathValidation()) {
    return;
  }

  Network::Address::InstanceConstSharedPtr local_addr;
  Network::Address::InstanceConstSharedPtr& remote_address = connection_socket->connectionInfoProvider().remoteAddress();
  auto probing_socket = createConnectionSocket(remote_address, local_addr, nullptr);
  setUpConnectionSocket(probing_socket, delegate_);
  auto writer = std::make_unique<EnvoyQuicPacketWriter>(
      std::make_unique<Network::UdpDefaultWriter>(probing_socket->ioHandle()));
  quic::QuicSocketAddress self_address = envoyIpAddressToQuicSocketAddress(
      probing_socket->connectionInfoProvider().localAddress()->ip());
  quic::QuicSocketAddress peer_address = envoyIpAddressToQuicSocketAddress(
      probing_socket->connectionInfoProvider().remoteAddress()->ip());

  auto context = std::make_unique<EnvoyQuicPathValidationContext>(self_address, peer_address, std::move(probing_socket), std::move(writer));
  ValidatePath(std::move(context), std::make_unique<EnvoyPathValidationResultDelegate>(*this));
}

void EnvoyQuicClientConnection::OnPathValidationSuccess(std::unique_ptr<quic::QuicPathValidationContext> context) {
  setConnectionSocket(std::move(context->ReleaseSocket()));
  MigratePath(context->self_address(), context->peer_address(), context->ReleaseWriter().release(), true);
}


void EnvoyQuicClientConnection::OnPathValidationFailure(std::unique_ptr<quic::QuicPathValidationContext> context) {
  OnpathValidationFailureAtClient();
  CancelPathValidation();
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

EnvoyQuicPathValidationContext::EnvoyQuicPathValidationContext(quic::QuicSocketAddress& self_address, quic::QuicSocketAddress& peer_address, 
    Network::ConnectionSocketPtr connectionSocket, std::unique_ptr<EnvoyQuicPacketWriter> writer) : QuicPathValidationContext(self_address, peer_address),
      socket_(std::move(connectionSocket)), writer_(std::move(writer)) {}

~EnvoyQuicPathValidationContext::EnvoyQuicPathValidationContext() = default;

quic::QuicPacketWriter* EnvoyQuicPathValidationContext::WriterToUse() {
  return writer_.get();
}

std::unique_ptr<EnvoyQuicPacketWriter> EnvoyQuicPathValidationContext::ReleaseWriter() {
  return std::move(writer_);
}

Network::ConnectionSocketPtr EnvoyQuicPathValidationContext::ReleaseSocket() {
  return std::move(socket_);
}

EnvoyPathValidationResultDelegate::EnvoyPathValidationResultDelegate(EnvoyQuicClientConnection& connection)
  : connection_(connection) {}

void EnvoyPathValidationResultDelegate::OnPathValidationSuccess(std::unique_ptr<quic::QuicPathValidationContext> context) {
  connection_.OnPathValidationSuccess(std::move(context));
}


void EnvoyPathValidationResultDelegate::OnPathValidationFailure(std::unique_ptr<quic::QuicPathValidationContext> context) {
  connection_.OnPathValidationFailure(std::move(context));
}

} // namespace Quic
} // namespace Envoy
