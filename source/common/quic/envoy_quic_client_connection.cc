#include "source/common/quic/envoy_quic_client_connection.h"

#include <memory>

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "quiche/quic/platform/api/quic_stack_trace.h"

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
      QuicNetworkConnection(std::move(connection_socket)), dispatcher_(dispatcher) {
        std::cout << "envoy connection created!!!" << std::endl;
      }

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

void EnvoyQuicClientConnection::setUpConnectionSocket(Network::ConnectionSocket& connection_socket, OptRef<PacketsToReadDelegate> delegate) {
  //std::cout << quic::QuicStackTrace() << std::endl;
  std::cout << "setting up socket" << std::endl;
  delegate_ = delegate;
  if (connection_socket.ioHandle().isOpen()) {
    connection_socket.ioHandle().initializeFileEvent(
        dispatcher_, [this](uint32_t events) -> void { onFileEvent(events); },
        Event::PlatformDefaultTriggerType,
        Event::FileReadyType::Read | Event::FileReadyType::Write);

    if (!Network::Socket::applyOptions(connection_socket.options(), connection_socket,
                                       envoy::config::core::v3::SocketOption::STATE_LISTENING)) {
      ENVOY_CONN_LOG(error, "Fail to apply listening options", *this);
      connection_socket.close();
    }
    std::cout << "socket setup complete" << std::endl;
  }
  if (!connection_socket.ioHandle().isOpen()) {
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

  // The old socket is not closed in this call.
  setConnectionSocket(std::move(connection_socket));
  setUpConnectionSocket(*connectionSocket(), delegate_);
  if (connection_migration_use_new_cid()) {
    MigratePath(self_address, peer_address, writer.release(), true);
  } else {
    SetQuicPacketWriter(writer.release(), true);
  }
}

void EnvoyQuicClientConnection::OnPathDegradingDetected() {
  QuicConnection::OnPathDegradingDetected();
  //MaybeMigratePort();
}

void EnvoyQuicClientConnection::MaybeMigratePort(Network::Address::InstanceConstSharedPtr& addr) {
  if (!IsHandshakeConfirmed() /*|| !connection_migration_use_new_cid()*/ || HasPendingPathValidation()) {
    std::cout << "port migration not attended" << std::endl;
    return;
  }

  std::cout << "prepare for migration" << std::endl;
  std::cout << "current local address " << connectionSocket()->connectionInfoProvider().localAddress()->asString() << std::endl;

  //Network::Address::InstanceConstSharedPtr local_addr;
  auto remote_address = const_cast<Network::Address::InstanceConstSharedPtr&>(connectionSocket()->connectionInfoProvider().remoteAddress());
  std::cout << "about to create new socket" << std::endl;
  probing_socket_ = createConnectionSocket(remote_address, addr, nullptr);
  std::cout << "about to call setUpConnectionSocket" << std::endl;
  setUpConnectionSocket(*probing_socket_, delegate_);
  auto writer = std::make_unique<EnvoyQuicPacketWriter>(
      std::make_unique<Network::UdpDefaultWriter>(probing_socket_->ioHandle()));
  quic::QuicSocketAddress self_address = envoyIpAddressToQuicSocketAddress(
      probing_socket_->connectionInfoProvider().localAddress()->ip());
  quic::QuicSocketAddress peer_address = envoyIpAddressToQuicSocketAddress(
      probing_socket_->connectionInfoProvider().remoteAddress()->ip());

  auto context = std::make_unique<EnvoyQuicPathValidationContext>(self_address, peer_address, std::move(writer));
  std::cout << "validating path on local addr " << self_address << std::endl << "peer addr " << peer_address << std::endl;
  ValidatePath(std::move(context), std::make_unique<EnvoyPathValidationResultDelegate>(*this));
}

void EnvoyQuicClientConnection::OnPathValidationSuccess(std::unique_ptr<quic::QuicPathValidationContext> context) {
  std::cout << "path validation success" << std::endl;
  auto envoy_context = static_cast<EnvoyQuicClientConnection::EnvoyQuicPathValidationContext*>(context.get());
  MigratePath(envoy_context->self_address(), envoy_context->peer_address(), envoy_context->ReleaseWriter().release(), true);
  setConnectionSocket(std::move(probing_socket_));
  std::cout << "after path migration" << std::endl;
}


void EnvoyQuicClientConnection::OnPathValidationFailure(std::unique_ptr<quic::QuicPathValidationContext> /*context*/) {
  std::cout << "path validation failed" << std::endl;
  OnPathValidationFailureAtClient();
  CancelPathValidation();
}

void EnvoyQuicClientConnection::onFileEvent(uint32_t events) {
  if (events == 1) {
    //std::cout << quic::QuicStackTrace() << std::endl;
  }
  std::cout << "client connection file event " << events << std::endl;
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
    if (!connectionSocket()) {
      return;
    }
    Api::IoErrorPtr err = Network::Utility::readPacketsFromSocket(
        connectionSocket()->ioHandle(),
        *connectionSocket()->connectionInfoProvider().localAddress(), *this,
        dispatcher_.timeSource(), true, packets_dropped_);
    std::cout << "done reading from " << connectionSocket()->ioHandle().fdDoNotUse() << std::endl;
    if (err == nullptr) {
      if (!connectionSocket()) {
        std::cout << "socket disappeared" << std::endl;
        return;
      }
      std::cout << "at here!!!!!!!!!!!!!!!!!" << std::endl;
      connectionSocket()->ioHandle().activateFileEvents(Event::FileReadyType::Read);
      std::cout << "past here!!!!!!!!!!!!!!!!!" << std::endl;
    }
    std::cout << "error is nullptr??" << (err ==nullptr) <<std::endl;
    if (err->getErrorCode() != Api::IoError::IoErrorCode::Again) {
      ENVOY_CONN_LOG(error, "recvmsg result {}: {}", *this, static_cast<int>(err->getErrorCode()),
                     err->getErrorDetails());
      std::cout << "passed logging" <<std::endl;
    }
    std::cout << "reached return" << std::endl;
    return;

    err = Network::Utility::readPacketsFromSocket(
        probing_socket_->ioHandle(),
        *probing_socket_->connectionInfoProvider().localAddress(), *this,
        dispatcher_.timeSource(), true, packets_dropped_);
    if (err == nullptr) {
      std::cout << "probing socket moved ? " << (probing_socket_ == nullptr) << std::endl;
      probing_socket_->ioHandle().activateFileEvents(Event::FileReadyType::Read);
      return;
    }
    if (err->getErrorCode() != Api::IoError::IoErrorCode::Again) {
      ENVOY_CONN_LOG(error, "recvmsg result {}: {}", *this, static_cast<int>(err->getErrorCode()),
                     err->getErrorDetails());
    }
  }
}

EnvoyQuicClientConnection::EnvoyQuicPathValidationContext::EnvoyQuicPathValidationContext(quic::QuicSocketAddress& self_address, quic::QuicSocketAddress& peer_address, 
    std::unique_ptr<EnvoyQuicPacketWriter> writer) : QuicPathValidationContext(self_address, peer_address), writer_(std::move(writer)) {}

EnvoyQuicClientConnection::EnvoyQuicPathValidationContext::~EnvoyQuicPathValidationContext() = default;

quic::QuicPacketWriter* EnvoyQuicClientConnection::EnvoyQuicPathValidationContext::WriterToUse() {
  return writer_.get();
}

std::unique_ptr<EnvoyQuicPacketWriter> EnvoyQuicClientConnection::EnvoyQuicPathValidationContext::ReleaseWriter() {
  return std::move(writer_);
}

EnvoyQuicClientConnection::EnvoyPathValidationResultDelegate::EnvoyPathValidationResultDelegate(EnvoyQuicClientConnection& connection)
  : connection_(connection) {}

void EnvoyQuicClientConnection::EnvoyPathValidationResultDelegate::OnPathValidationSuccess(std::unique_ptr<quic::QuicPathValidationContext> context) {
  connection_.OnPathValidationSuccess(std::move(context));
}


void EnvoyQuicClientConnection::EnvoyPathValidationResultDelegate::OnPathValidationFailure(std::unique_ptr<quic::QuicPathValidationContext> context) {
  connection_.OnPathValidationFailure(std::move(context));
}

} // namespace Quic
} // namespace Envoy
