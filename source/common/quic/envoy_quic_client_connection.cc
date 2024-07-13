#include "source/common/quic/envoy_quic_client_connection.h"

#include <memory>

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/network/socket_option_factory.h"
#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Quic {

// Used to defer deleting connection socket to avoid deleting IoHandle in a read loop.
class DeferredDeletableSocket : public Event::DeferredDeletable {
public:
  explicit DeferredDeletableSocket(std::unique_ptr<Network::ConnectionSocket> socket)
      : socket_(std::move(socket)) {}

  void deleteIsPending() override { socket_->close(); }

private:
  std::unique_ptr<Network::ConnectionSocket> socket_;
};

EnvoyQuicClientConnection::EnvoyQuicClientConnection(
    const quic::QuicConnectionId& server_connection_id,
    Network::Address::InstanceConstSharedPtr& initial_peer_address,
    quic::QuicConnectionHelperInterface& helper, quic::QuicAlarmFactory& alarm_factory,
    const quic::ParsedQuicVersionVector& supported_versions,
    Network::Address::InstanceConstSharedPtr local_addr, Event::Dispatcher& dispatcher,
    const Network::ConnectionSocket::OptionsSharedPtr& options,
    quic::ConnectionIdGeneratorInterface& generator, const bool prefer_gro)
    : EnvoyQuicClientConnection(
          server_connection_id, helper, alarm_factory, supported_versions, dispatcher,
          createConnectionSocket(initial_peer_address, local_addr, options, prefer_gro), generator,
          prefer_gro) {}

EnvoyQuicClientConnection::EnvoyQuicClientConnection(
    const quic::QuicConnectionId& server_connection_id, quic::QuicConnectionHelperInterface& helper,
    quic::QuicAlarmFactory& alarm_factory, const quic::ParsedQuicVersionVector& supported_versions,
    Event::Dispatcher& dispatcher, Network::ConnectionSocketPtr&& connection_socket,
    quic::ConnectionIdGeneratorInterface& generator, const bool prefer_gro)
    : EnvoyQuicClientConnection(
          server_connection_id, helper, alarm_factory,
          new EnvoyQuicPacketWriter(
              std::make_unique<Network::UdpDefaultWriter>(connection_socket->ioHandle())),
          /*owns_writer=*/true, supported_versions, dispatcher, std::move(connection_socket),
          generator, prefer_gro) {}

EnvoyQuicClientConnection::EnvoyQuicClientConnection(
    const quic::QuicConnectionId& server_connection_id, quic::QuicConnectionHelperInterface& helper,
    quic::QuicAlarmFactory& alarm_factory, quic::QuicPacketWriter* writer, bool owns_writer,
    const quic::ParsedQuicVersionVector& supported_versions, Event::Dispatcher& dispatcher,
    Network::ConnectionSocketPtr&& connection_socket,
    quic::ConnectionIdGeneratorInterface& generator, const bool prefer_gro)
    : quic::QuicConnection(server_connection_id, quic::QuicSocketAddress(),
                           envoyIpAddressToQuicSocketAddress(
                               connection_socket->connectionInfoProvider().remoteAddress()->ip()),
                           &helper, &alarm_factory, writer, owns_writer,
                           quic::Perspective::IS_CLIENT, supported_versions, generator),
      QuicNetworkConnection(std::move(connection_socket)), dispatcher_(dispatcher),
      prefer_gro_(prefer_gro), disallow_mmsg_(Runtime::runtimeFeatureEnabled(
                                   "envoy.reloadable_features.disallow_quic_client_udp_mmsg")) {}

void EnvoyQuicClientConnection::processPacket(
    Network::Address::InstanceConstSharedPtr local_address,
    Network::Address::InstanceConstSharedPtr peer_address, Buffer::InstancePtr buffer,
    MonotonicTime receive_time, uint8_t tos) {
  quic::QuicTime timestamp =
      quic::QuicTime::Zero() +
      quic::QuicTime::Delta::FromMicroseconds(
          std::chrono::duration_cast<std::chrono::microseconds>(receive_time.time_since_epoch())
              .count());
  ASSERT(peer_address != nullptr && buffer != nullptr);
  ASSERT(buffer->getRawSlices().size() == 1);
  if (local_address == nullptr) {
    // Quic doesn't know how to handle packets without destination address. Drop them here.
    if (buffer->length() > 0) {
      ++num_packets_with_unknown_dst_address_;
      std::string error_message = fmt::format(
          "Unable to get destination address. Address family {}. Have{} pending path validation. "
          "self_address is{} initialized.",
          (peer_address->ip()->version() == Network::Address::IpVersion::v4 ? "v4" : "v6"),
          (HasPendingPathValidation() ? "" : " no"),
          (self_address().IsInitialized() ? "" : " not"));
      ENVOY_CONN_LOG(error, "{}", *this, error_message);
      if (num_packets_with_unknown_dst_address_ > 10) {
        // If too many packets are without destination addresses, close the connection.
        CloseConnection(quic::QUIC_PACKET_READ_ERROR, error_message,
                        quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      }
    }
    return;
  }
  Buffer::RawSlice slice = buffer->frontSlice();
  quic::QuicReceivedPacket packet(reinterpret_cast<char*>(slice.mem_), slice.len_, timestamp,
                                  /*owns_buffer=*/false, /*ttl=*/0, /*ttl_valid=*/false,
                                  /*packet_headers=*/nullptr, /*headers_length=*/0,
                                  /*owns_header_buffer*/ false,
                                  getQuicEcnCodepointFromTosByte(tos));
  ProcessUdpPacket(envoyIpAddressToQuicSocketAddress(local_address->ip()),
                   envoyIpAddressToQuicSocketAddress(peer_address->ip()), packet);
}

uint64_t EnvoyQuicClientConnection::maxDatagramSize() const {
  // TODO(danzh) make this variable configurable to support jumbo frames.
  return Network::DEFAULT_UDP_MAX_DATAGRAM_SIZE;
}

void EnvoyQuicClientConnection::setUpConnectionSocket(Network::ConnectionSocket& connection_socket,
                                                      OptRef<PacketsToReadDelegate> delegate) {
  delegate_ = delegate;
  if (connection_socket.isOpen()) {
    connection_socket.ioHandle().initializeFileEvent(
        dispatcher_,
        [this, &connection_socket](uint32_t events) {
          onFileEvent(events, connection_socket);
          return absl::OkStatus();
        },
        Event::PlatformDefaultTriggerType,
        Event::FileReadyType::Read | Event::FileReadyType::Write);

    if (!Network::Socket::applyOptions(connection_socket.options(), connection_socket,
                                       envoy::config::core::v3::SocketOption::STATE_LISTENING)) {
      ENVOY_CONN_LOG(error, "Fail to apply listening options", *this);
      connection_socket.close();
    }
  }
  if (!connection_socket.isOpen()) {
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

  // The old socket is not closed in this call, because it could still receive useful packets.
  num_socket_switches_++;
  setConnectionSocket(std::move(connection_socket));
  setUpConnectionSocket(*connectionSocket(), delegate_);
  MigratePath(self_address, peer_address, writer.release(), true);
}

void EnvoyQuicClientConnection::OnPathDegradingDetected() {
  QuicConnection::OnPathDegradingDetected();
  maybeMigratePort();
}

void EnvoyQuicClientConnection::maybeMigratePort() {
  if (!IsHandshakeConfirmed() || HasPendingPathValidation() || !migrate_port_on_path_degrading_ ||
      num_socket_switches_ >= kMaxNumSocketSwitches) {
    return;
  }

  probeWithNewPort(peer_address(), quic::PathValidationReason::kPortMigration);
}

void EnvoyQuicClientConnection::probeWithNewPort(const quic::QuicSocketAddress& peer_address,
                                                 quic::PathValidationReason reason) {
  const Network::Address::InstanceConstSharedPtr& current_local_address =
      connectionSocket()->connectionInfoProvider().localAddress();
  // Creates an IP address with unset port. The port will be set when the new socket is created.
  Network::Address::InstanceConstSharedPtr new_local_address;
  if (current_local_address->ip()->version() == Network::Address::IpVersion::v4) {
    new_local_address = std::make_shared<Network::Address::Ipv4Instance>(
        current_local_address->ip()->addressAsString(), &current_local_address->socketInterface());
  } else {
    new_local_address = std::make_shared<Network::Address::Ipv6Instance>(
        current_local_address->ip()->addressAsString(), &current_local_address->socketInterface());
  }

  // The probing socket will have the same host but a different port.
  auto probing_socket =
      createConnectionSocket(connectionSocket()->connectionInfoProvider().remoteAddress(),
                             new_local_address, connectionSocket()->options(), prefer_gro_);
  setUpConnectionSocket(*probing_socket, delegate_);
  auto writer = std::make_unique<EnvoyQuicPacketWriter>(
      std::make_unique<Network::UdpDefaultWriter>(probing_socket->ioHandle()));
  quic::QuicSocketAddress self_address = envoyIpAddressToQuicSocketAddress(
      probing_socket->connectionInfoProvider().localAddress()->ip());

  auto context = std::make_unique<EnvoyQuicPathValidationContext>(
      self_address, peer_address, std::move(writer), std::move(probing_socket));
  ValidatePath(std::move(context), std::make_unique<EnvoyPathValidationResultDelegate>(*this),
               reason);
}

void EnvoyQuicClientConnection::onPathValidationSuccess(
    std::unique_ptr<quic::QuicPathValidationContext> context) {
  auto envoy_context =
      static_cast<EnvoyQuicClientConnection::EnvoyQuicPathValidationContext*>(context.get());

  auto probing_socket = envoy_context->releaseSocket();
  if (envoy_context->peer_address() != peer_address()) {
    OnServerPreferredAddressValidated(*envoy_context, true);
    envoy_context->releaseWriter();
  } else {
    MigratePath(envoy_context->self_address(), envoy_context->peer_address(),
                envoy_context->releaseWriter(), true);
  }

  if (self_address() == envoy_context->self_address() &&
      peer_address() == envoy_context->peer_address()) {
    // probing_socket will be set as the new default socket. But old sockets are still able to
    // receive packets.
    num_socket_switches_++;
    setConnectionSocket(std::move(probing_socket));
    return;
  }
  // MigratePath should always succeed since the migration happens after path
  // validation.
  ENVOY_CONN_LOG(error, "connection fails to migrate path after validation", *this);
}

void EnvoyQuicClientConnection::onPathValidationFailure(
    std::unique_ptr<quic::QuicPathValidationContext> context) {
  // Note that the probing socket and probing writer will be deleted once context goes out of
  // scope.
  OnPathValidationFailureAtClient(/*is_multi_port=*/false, *context);
  std::unique_ptr<Network::ConnectionSocket> probing_socket =
      static_cast<EnvoyQuicPathValidationContext*>(context.get())->releaseSocket();
  // Extend the socket life time till the end of the current event loop.
  dispatcher_.deferredDelete(std::make_unique<DeferredDeletableSocket>(std::move(probing_socket)));
}

void EnvoyQuicClientConnection::onFileEvent(uint32_t events,
                                            Network::ConnectionSocket& connection_socket) {
  ENVOY_CONN_LOG(trace, "socket event: {}", *this, events);
  ASSERT(events & (Event::FileReadyType::Read | Event::FileReadyType::Write));

  if (events & Event::FileReadyType::Write) {
    OnCanWrite();
  }

  bool is_probing_socket =
      HasPendingPathValidation() &&
      (&connection_socket ==
       &static_cast<EnvoyQuicClientConnection::EnvoyQuicPathValidationContext*>(
            GetPathValidationContext())
            ->probingSocket());

  // It's possible for a write event callback to close the connection, in such case ignore read
  // event processing.
  // TODO(mattklein123): Right now QUIC client is hard coded to use GRO because it is probably the
  // right default for QUIC. Determine whether this should be configurable or not.
  if (connected() && (events & Event::FileReadyType::Read)) {
    Api::IoErrorPtr err = Network::Utility::readPacketsFromSocket(
        connection_socket.ioHandle(), *connection_socket.connectionInfoProvider().localAddress(),
        *this, dispatcher_.timeSource(), prefer_gro_, !disallow_mmsg_, packets_dropped_);
    if (err == nullptr) {
      // In the case where the path validation fails, the probing socket will be closed and its IO
      // events are no longer interesting.
      if (!is_probing_socket || HasPendingPathValidation() ||
          connectionSocket().get() == &connection_socket) {
        connection_socket.ioHandle().activateFileEvents(Event::FileReadyType::Read);
        return;
      }

    } else if (err->getErrorCode() != Api::IoError::IoErrorCode::Again) {
      ENVOY_CONN_LOG(error, "recvmsg result {}: {}", *this, static_cast<int>(err->getErrorCode()),
                     err->getErrorDetails());
    }
  }
}

void EnvoyQuicClientConnection::setNumPtosForPortMigration(uint32_t num_ptos_for_path_degrading) {
  if (num_ptos_for_path_degrading < 1) {
    return;
  }
  migrate_port_on_path_degrading_ = true;
  sent_packet_manager().set_num_ptos_for_path_degrading(num_ptos_for_path_degrading);
}

EnvoyQuicClientConnection::EnvoyQuicPathValidationContext::EnvoyQuicPathValidationContext(
    const quic::QuicSocketAddress& self_address, const quic::QuicSocketAddress& peer_address,
    std::unique_ptr<EnvoyQuicPacketWriter> writer,
    std::unique_ptr<Network::ConnectionSocket> probing_socket)
    : QuicPathValidationContext(self_address, peer_address), writer_(std::move(writer)),
      socket_(std::move(probing_socket)) {}

EnvoyQuicClientConnection::EnvoyQuicPathValidationContext::~EnvoyQuicPathValidationContext() =
    default;

quic::QuicPacketWriter* EnvoyQuicClientConnection::EnvoyQuicPathValidationContext::WriterToUse() {
  return writer_.get();
}

EnvoyQuicPacketWriter* EnvoyQuicClientConnection::EnvoyQuicPathValidationContext::releaseWriter() {
  return writer_.release();
}

std::unique_ptr<Network::ConnectionSocket>
EnvoyQuicClientConnection::EnvoyQuicPathValidationContext::releaseSocket() {
  return std::move(socket_);
}

Network::ConnectionSocket&
EnvoyQuicClientConnection::EnvoyQuicPathValidationContext::probingSocket() {
  return *socket_;
}

EnvoyQuicClientConnection::EnvoyPathValidationResultDelegate::EnvoyPathValidationResultDelegate(
    EnvoyQuicClientConnection& connection)
    : connection_(connection) {}

void EnvoyQuicClientConnection::EnvoyPathValidationResultDelegate::OnPathValidationSuccess(
    std::unique_ptr<quic::QuicPathValidationContext> context, quic::QuicTime /*start_time*/) {
  connection_.onPathValidationSuccess(std::move(context));
}

void EnvoyQuicClientConnection::EnvoyPathValidationResultDelegate::OnPathValidationFailure(
    std::unique_ptr<quic::QuicPathValidationContext> context) {
  connection_.onPathValidationFailure(std::move(context));
}

void EnvoyQuicClientConnection::OnCanWrite() {
  quic::QuicConnection::OnCanWrite();
  onWriteEventDone();
}

void EnvoyQuicClientConnection::probeAndMigrateToServerPreferredAddress(
    const quic::QuicSocketAddress& server_preferred_address) {
  probeWithNewPort(server_preferred_address,
                   quic::PathValidationReason::kServerPreferredAddressMigration);
}

} // namespace Quic
} // namespace Envoy
