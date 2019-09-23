#include "extensions/quic_listeners/quiche/envoy_quic_client_connection.h"

#include "common/network/listen_socket_impl.h"

#include "extensions/quic_listeners/quiche/envoy_quic_packet_writer.h"
#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"
#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Quic {

EnvoyQuicClientConnection::EnvoyQuicClientConnection(
    quic::QuicConnectionId server_connection_id,
    Network::Address::InstanceConstSharedPtr& initial_peer_address,
    quic::QuicConnectionHelperInterface& helper, quic::QuicAlarmFactory& alarm_factory,
    const quic::ParsedQuicVersionVector& supported_versions,
    Network::Address::InstanceConstSharedPtr local_addr, Event::Dispatcher& dispatcher)
    : EnvoyQuicClientConnection(server_connection_id, helper, alarm_factory, supported_versions,
                                dispatcher,
                                createConnectionSocket(initial_peer_address, local_addr)) {}

EnvoyQuicClientConnection::EnvoyQuicClientConnection(
    quic::QuicConnectionId server_connection_id, quic::QuicConnectionHelperInterface& helper,
    quic::QuicAlarmFactory& alarm_factory, const quic::ParsedQuicVersionVector& supported_versions,
    Event::Dispatcher& dispatcher, Network::ConnectionSocketPtr&& connection_socket)
    : EnvoyQuicClientConnection(server_connection_id, helper, alarm_factory,
                                new EnvoyQuicPacketWriter(*connection_socket), true,
                                supported_versions, dispatcher, std::move(connection_socket)) {}

EnvoyQuicClientConnection::EnvoyQuicClientConnection(
    quic::QuicConnectionId server_connection_id, quic::QuicConnectionHelperInterface& helper,
    quic::QuicAlarmFactory& alarm_factory, quic::QuicPacketWriter* writer, bool owns_writer,
    const quic::ParsedQuicVersionVector& supported_versions, Event::Dispatcher& dispatcher,
    Network::ConnectionSocketPtr&& connection_socket)
    : EnvoyQuicConnection(
          server_connection_id,
          envoyAddressInstanceToQuicSocketAddress(connection_socket->remoteAddress()), helper,
          alarm_factory, writer, owns_writer, quic::Perspective::IS_CLIENT, supported_versions,
          std::move(connection_socket)),
      dispatcher_(dispatcher),
      file_event_(dispatcher.createFileEvent(
          connectionSocket()->ioHandle().fd(),
          [this](uint32_t events) -> void { onFileEvent(events); }, Event::FileTriggerType::Edge,
          Event::FileReadyType::Read | Event::FileReadyType::Write)) {}

EnvoyQuicClientConnection::~EnvoyQuicClientConnection() { file_event_->setEnabled(0); }

void EnvoyQuicClientConnection::processPacket(
    Network::Address::InstanceConstSharedPtr local_address,
    Network::Address::InstanceConstSharedPtr peer_address, Buffer::InstancePtr buffer,
    MonotonicTime receive_time) {
  quic::QuicTime timestamp =
      quic::QuicTime::Zero() +
      quic::QuicTime::Delta::FromMilliseconds(
          std::chrono::duration_cast<std::chrono::milliseconds>(receive_time.time_since_epoch())
              .count());
  uint64_t num_slice = buffer->getRawSlices(nullptr, 0);
  ASSERT(num_slice == 1);
  Buffer::RawSlice slice;
  buffer->getRawSlices(&slice, 1);
  quic::QuicReceivedPacket packet(reinterpret_cast<char*>(slice.mem_), slice.len_, timestamp,
                                  /*owns_buffer=*/false, /*ttl=*/0, /*ttl_valid=*/true,
                                  /*packet_headers=*/nullptr, /*headers_length=*/0,
                                  /*owns_header_buffer*/ false);
  ProcessUdpPacket(envoyAddressInstanceToQuicSocketAddress(local_address),
                   envoyAddressInstanceToQuicSocketAddress(peer_address), packet);
}

uint64_t EnvoyQuicClientConnection::maxPacketSize() const {
  // TODO(danzh) make this variable configurable to support jumbo frames.
  return Network::MAX_UDP_PACKET_SIZE;
}

void EnvoyQuicClientConnection::onFileEvent(uint32_t events) {
  ENVOY_CONN_LOG(trace, "socket event: {}", *this, events);
  ASSERT(events & (Event::FileReadyType::Read | Event::FileReadyType::Write));

  if (events & Event::FileReadyType::Write) {
    OnCanWrite();
  }

  // It's possible for a write event callback to close the connection, in such case ignore read
  // event processing.
  if (connected() && (events & Event::FileReadyType::Read)) {
    uint32_t old_packets_dropped = packets_dropped_;
    while (connected()) {
      // Read till socket is drained.
      // TODO(danzh): limit read times here.
      MonotonicTime receive_time = dispatcher_.timeSource().monotonicTime();
      Api::IoCallUint64Result result = Network::Utility::readFromSocket(
          *connectionSocket(), *this, &packets_dropped_, receive_time);
      if (!result.ok()) {
        if (result.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
          ENVOY_CONN_LOG(error, "recvmsg result {}: {}", *this,
                         static_cast<int>(result.err_->getErrorCode()),
                         result.err_->getErrorDetails());
        }
        // Stop reading.
        break;
      }

      if (result.rc_ == 0) {
        // TODO(conqerAtapple): Is zero length packet interesting? If so add stats
        // for it. Otherwise remove the warning log below.
        ENVOY_CONN_LOG(trace, "received 0-length packet", *this);
      }

      if (packets_dropped_ != old_packets_dropped) {
        // The kernel tracks SO_RXQ_OVFL as a uint32 which can overflow to a smaller
        // value. So as long as this count differs from previously recorded value,
        // more packets are dropped by kernel.
        uint32_t delta = (packets_dropped_ > old_packets_dropped)
                             ? (packets_dropped_ - old_packets_dropped)
                             : (packets_dropped_ +
                                (std::numeric_limits<uint32_t>::max() - old_packets_dropped) + 1);
        // TODO(danzh) add stats for this.
        ENVOY_CONN_LOG(debug,
                       "Kernel dropped {} more packets. Consider increase receive buffer size.",
                       *this, delta);
      }
    }
  }
}

Network::ConnectionSocketPtr EnvoyQuicClientConnection::createConnectionSocket(
    Network::Address::InstanceConstSharedPtr& peer_addr,
    Network::Address::InstanceConstSharedPtr& local_addr) {
  Network::IoHandlePtr io_handle = peer_addr->socket(Network::Address::SocketType::Datagram);
  local_addr->bind(io_handle->fd());
  if (local_addr->ip()->port() == 0) {
    // Get ephemeral port number.
    local_addr = Network::Address::addressFromFd(io_handle->fd());
  }
  return std::make_unique<Network::ConnectionSocketImpl>(std::move(io_handle), local_addr,
                                                         peer_addr);
}

} // namespace Quic
} // namespace Envoy
