#include "source/common/quic/envoy_quic_client_connection.h"

#include <memory>

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/network/socket_option_factory.h"
#include "source/common/network/udp_packet_writer_handler_impl.h"
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

class EnvoyQuicClientPathValidationContext : public quic::QuicClientPathValidationContext {
public:
  EnvoyQuicClientPathValidationContext(quic::QuicSocketAddress self_address,
                                       quic::QuicSocketAddress peer_address,
                                       quic::QuicNetworkHandle network,
                                       std::unique_ptr<EnvoyQuicPacketWriter>&& writer,
                                       Network::ConnectionSocketPtr&& socket,
                                       Event::Dispatcher& dispatcher)
      : quic::QuicClientPathValidationContext(self_address, peer_address, network),
        writer_(std::make_unique<quic::QuicForceBlockablePacketWriter>()),
        socket_(std::move(socket)), dispatcher_(dispatcher) {
    // Owns the writer.
    writer_->set_writer(writer.release());
  }

  ~EnvoyQuicClientPathValidationContext() override {
    if (socket_ != nullptr) {
      // The socket wasn't used by the connection, the path validation must have failed. Now
      // deferred delete it to avoid deleting IoHandle in a read loop.
      dispatcher_.deferredDelete(std::make_unique<DeferredDeletableSocket>(std::move(socket_)));
    }
  }

  bool ShouldConnectionOwnWriter() const override { return true; }
  quic::QuicForceBlockablePacketWriter* ForceBlockableWriterToUse() override {
    return writer_.get();
  }

  Network::ConnectionSocket& probingSocket() { return *socket_; }

  quic::QuicForceBlockablePacketWriter* releaseWriter() { return writer_.release(); }
  std::unique_ptr<Network::ConnectionSocket> releaseSocket() { return std::move(socket_); }

private:
  std::unique_ptr<quic::QuicForceBlockablePacketWriter> writer_;
  Network::ConnectionSocketPtr socket_;
  Event::Dispatcher& dispatcher_;
};

void EnvoyQuicClientConnection::EnvoyQuicClinetPathContextFactory::CreatePathValidationContext(
    quic::QuicNetworkHandle network, quic::QuicSocketAddress peer_address,
    std::unique_ptr<quic::QuicPathContextFactory::CreationResultDelegate> result_delegate) {
  Network::Address::InstanceConstSharedPtr new_local_address;
  if (network == quic::kInvalidNetworkHandle) {
    // If there isn't a meaningful network handle to bind to, bind to the
    // local address of the current socket.
    Network::Address::InstanceConstSharedPtr current_local_address =
        connection_.connectionSocket()->connectionInfoProvider().localAddress();
    if (current_local_address->ip()->version() == Network::Address::IpVersion::v4) {
      new_local_address = std::make_shared<Network::Address::Ipv4Instance>(
          current_local_address->ip()->addressAsString(),
          &current_local_address->socketInterface());
    } else {
      new_local_address = std::make_shared<Network::Address::Ipv6Instance>(
          current_local_address->ip()->addressAsString(),
          &current_local_address->socketInterface());
    }
  }
  Network::Address::InstanceConstSharedPtr remote_address =
      (connection_.peer_address() == peer_address)
          ? connection_.connectionSocket()->connectionInfoProvider().remoteAddress()
          : quicAddressToEnvoyAddressInstance(peer_address);
  // new_local_address will be re-assigned if it is nullptr.
  QuicClientPacketWriterFactory::CreationResult result =
      writer_factory_.createSocketAndQuicPacketWriter(remote_address, network, new_local_address,
                                                      connection_.connectionSocket()->options());
  connection_.setUpConnectionSocket(*result.socket_, connection_.delegate_);
  result_delegate->OnCreationSucceeded(std::make_unique<EnvoyQuicClientPathValidationContext>(
      envoyIpAddressToQuicSocketAddress(new_local_address->ip()), peer_address, network,
      std::move(result.writer_), std::move(result.socket_), connection_.dispatcher_));
}

quic::QuicNetworkHandle EnvoyQuicClientConnection::EnvoyQuicMigrationHelper::FindAlternateNetwork(
    quic::QuicNetworkHandle /*network*/) {
  return quic::kInvalidNetworkHandle;
}

quic::QuicNetworkHandle EnvoyQuicClientConnection::EnvoyQuicMigrationHelper::GetDefaultNetwork() {
  return quic::kInvalidNetworkHandle;
}

void EnvoyQuicClientConnection::EnvoyQuicMigrationHelper::OnMigrationToPathDone(
    std::unique_ptr<quic::QuicClientPathValidationContext> context, bool success) {
  if (success) {
    auto* envoy_context = static_cast<EnvoyQuicClientPathValidationContext*>(context.get());
    // Connection already owns the writer.
    envoy_context->releaseWriter();
    ++connection_.num_socket_switches_;
    connection_.setConnectionSocket(envoy_context->releaseSocket());
    // Previous writer may have been force blocked and write events on it may have been dropped.
    // Synthesize a write event in case this case to unblock the connection.
    connection_.connectionSocket()->ioHandle().activateFileEvents(Event::FileReadyType::Write);
    // Send something to notify the peer of the address change immediately.
    connection_.SendPing();
  }
}

std::unique_ptr<quic::QuicPathContextFactory>
EnvoyQuicClientConnection::EnvoyQuicMigrationHelper::CreateQuicPathContextFactory() {
  return std::make_unique<EnvoyQuicClinetPathContextFactory>(writer_factory_, connection_);
}

EnvoyQuicClientConnection::EnvoyQuicClientConnection(
    const quic::QuicConnectionId& server_connection_id, quic::QuicConnectionHelperInterface& helper,
    quic::QuicAlarmFactory& alarm_factory, quic::QuicPacketWriter* writer, bool owns_writer,
    const quic::ParsedQuicVersionVector& supported_versions, Event::Dispatcher& dispatcher,
    Network::ConnectionSocketPtr&& connection_socket,
    quic::ConnectionIdGeneratorInterface& generator)
    : quic::QuicConnection(server_connection_id, quic::QuicSocketAddress(),
                           envoyIpAddressToQuicSocketAddress(
                               connection_socket->connectionInfoProvider().remoteAddress()->ip()),
                           &helper, &alarm_factory, writer, owns_writer,
                           quic::Perspective::IS_CLIENT, supported_versions, generator),
      QuicNetworkConnection(std::move(connection_socket)), dispatcher_(dispatcher),
      disallow_mmsg_(Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.disallow_quic_client_udp_mmsg")) {}

void EnvoyQuicClientConnection::processPacket(
    Network::Address::InstanceConstSharedPtr local_address,
    Network::Address::InstanceConstSharedPtr peer_address, Buffer::InstancePtr buffer,
    MonotonicTime receive_time, uint8_t tos, Buffer::OwnedImpl /*saved_cmsg*/) {
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
  if (!connection_socket.isOpen() && connectionSocket().get() == &connection_socket) {
    // Only close the connection if the connection socket is the current one. If it is a probing
    // socket that isn't the current socket, do not close the connection upon failure, as the
    // current socket is still usable.
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
  // This will trigger connection migration or port migration in QUICHE if
  // migration_helper_ is initialized. Otherwise do it in this class.
  QuicConnection::OnPathDegradingDetected();
  if (migration_helper_ == nullptr) {
    maybeMigratePort();
  }
}

void EnvoyQuicClientConnection::maybeMigratePort() {
  if (!IsHandshakeConfirmed() || HasPendingPathValidation() || !migrate_port_on_path_degrading_ ||
      num_socket_switches_ >= kMaxNumSocketSwitches) {
    return;
  }

  probeWithNewPort(peer_address(), quic::PathValidationReason::kPortMigration);
}

void EnvoyQuicClientConnection::probeWithNewPort(const quic::QuicSocketAddress& peer_addr,
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
  ASSERT(migration_helper_ == nullptr && writer_factory_.has_value());
  QuicClientPacketWriterFactory::CreationResult creation_result =
      writer_factory_->createSocketAndQuicPacketWriter(
          (peer_addr == peer_address()
               ? connectionSocket()->connectionInfoProvider().remoteAddress()
               : quicAddressToEnvoyAddressInstance(peer_addr)),
          quic::kInvalidNetworkHandle, new_local_address, connectionSocket()->options());
  setUpConnectionSocket(*creation_result.socket_, delegate_);
  auto writer = std::move(creation_result.writer_);
  quic::QuicSocketAddress self_address = envoyIpAddressToQuicSocketAddress(
      creation_result.socket_->connectionInfoProvider().localAddress()->ip());

  auto context = std::make_unique<EnvoyQuicPathValidationContext>(
      self_address, peer_addr, std::move(writer), std::move(creation_result.socket_));
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
    writer()->SetWritable();
    // The writer might still be force blocked for migration in progress, in
    // which case no write should be attempted.
    WriteIfNotBlocked();
  }

  // Check if the event is on the probing socket before read.
  const bool is_probing_socket =
      HasPendingPathValidation() &&
      (&connection_socket ==
       (writer_factory_
            ? &static_cast<EnvoyQuicPathValidationContext*>(GetPathValidationContext())
                   ->probingSocket()
            : &static_cast<EnvoyQuicClientPathValidationContext*>(GetPathValidationContext())
                   ->probingSocket()));

  // It's possible for a write event callback to close the connection, in such case ignore read
  // event processing.
  // TODO(mattklein123): Right now QUIC client is hard coded to use GRO because it is probably the
  // right default for QUIC. Determine whether this should be configurable or not.
  if (connected() && (events & Event::FileReadyType::Read)) {
    Api::IoErrorPtr err = Network::Utility::readPacketsFromSocket(
        connection_socket.ioHandle(), *connection_socket.connectionInfoProvider().localAddress(),
        *this, dispatcher_.timeSource(), /*allow_gro=*/true, !disallow_mmsg_, packets_dropped_);
    if (err == nullptr) {
      // If this READ event is on the probing socket and any packet read failed the path validation
      // (i.e. via STATELESS_RESET), the probing socket should have been closed and the default
      // socket remained unchanged. In this case any remaining unread packets are no longer
      // interesting. Only re-register READ event to continue reading the remaining packets in the
      // next loop if this is not the case.
      if (!(is_probing_socket && !HasPendingPathValidation() &&
            connectionSocket().get() != &connection_socket)) {
        connection_socket.ioHandle().activateFileEvents(Event::FileReadyType::Read);
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

EnvoyQuicClientConnection::EnvoyQuicMigrationHelper&
EnvoyQuicClientConnection::getOrCreateMigrationHelper(
    QuicClientPacketWriterFactory& writer_factory,
    OptRef<EnvoyQuicNetworkObserverRegistry> registry) {
  if (migration_helper_ == nullptr) {
    migration_helper_ = std::make_unique<EnvoyQuicMigrationHelper>(*this, registry, writer_factory);
  }
  return *migration_helper_;
}

void EnvoyQuicClientConnection::probeAndMigrateToServerPreferredAddress(
    const quic::QuicSocketAddress& server_preferred_address) {
  ASSERT(migration_helper_ == nullptr);
  probeWithNewPort(server_preferred_address,
                   quic::PathValidationReason::kServerPreferredAddressMigration);
}

} // namespace Quic
} // namespace Envoy
