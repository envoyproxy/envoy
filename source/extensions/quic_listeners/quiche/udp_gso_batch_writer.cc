#include "extensions/quic_listeners/quiche/udp_gso_batch_writer.h"

#include "common/network/io_socket_error_impl.h"

#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"

namespace Envoy {
namespace Quic {

// Initialize QuicGsoBatchWriter, and set socket_
UdpGsoBatchWriter::UdpGsoBatchWriter(Network::Socket& socket)
    : quic::QuicGsoBatchWriter(std::make_unique<quic::QuicBatchWriterBuffer>(),
                               socket.ioHandle().fd()) {}

// Do Nothing in the Destructor For now
UdpGsoBatchWriter::~UdpGsoBatchWriter() {}

Api::IoCallUint64Result
UdpGsoBatchWriter::writeToSocket(const Buffer::Instance& buffer,
                                 const Network::Address::Ip* local_ip,
                                 const Network::Address::Instance& peer_address) {
  // Convert received parameters to relevant forms
  // TODO(yugant): Is there a better way to create it? Use common function that uses Instance.
  Network::Address::InstanceConstSharedPtr enpr =
      Network::Address::InstanceConstSharedPtr(&peer_address);
  quic::QuicSocketAddress peer_addr = envoyAddressInstanceToQuicSocketAddress(enpr);
  quic::QuicIpAddress self_ip = envoyAddressIpToQuicIpAddress(local_ip);

  // TODO(yugant):
  // If QUIC Then: define PerPacketOptions
  // Take extra parameter to writeToSocket and use it to create PerPacketOptions
  // Also we are not taking care of setting write_blocked_ (if needed) over here, as the
  // WritePacket implementation will do that for us.
  quic::WriteResult quic_result = WritePacket(
      buffer.toString().c_str(), static_cast<size_t>(buffer.length()), self_ip, peer_addr,
      /*quic::PerPacketOptions=*/nullptr);

  if (quic_result.status == quic::WRITE_STATUS_OK) {
    // Write Successful
    if (quic_result.bytes_written == 0) {
      // TODO(yugant): Add bytes buffered stats, +bytesLen
      ENVOY_LOG_MISC(trace, "sendmsg successful, message buffered to send");
    } else {
      // TODO(yugant): Use current Bytes Buffered
      // Add bytes sent stats, +bytesSent
      ENVOY_LOG_MISC(trace, "sendmsg successful, flushed bytes {}", quic_result.bytes_written);
    }
    // Return bytes_written as rc & nullptr as error on success
    return Api::IoCallUint64Result(
        /*rc=*/quic_result.bytes_written,
        /*err=*/Api::IoErrorPtr(nullptr, Network::IoSocketError::deleteIoError));
  }

  if (quic_result.status == quic::WRITE_STATUS_BLOCKED_DATA_BUFFERED) {
    // Data was buffered, no need to return error
    ENVOY_LOG_MISC(trace, "sendmsg blocked, message buffered to send");
    return Api::IoCallUint64Result(
        /*rc=*/0,
        /*err=*/Api::IoErrorPtr(nullptr, Network::IoSocketError::deleteIoError));
  }

  if (quic_result.status == quic::WRITE_STATUS_BLOCKED) {
    ENVOY_LOG_MISC(trace, "sendmsg blocked, message not buffered");
    return Api::IoCallUint64Result(
        /*rc=*/0,
        /*err=*/Api::IoErrorPtr(new Network::IoSocketError(quic_result.error_code),
                                Network::IoSocketError::deleteIoError));
  }

  // Write Failed, return {0 and error_code}
  ENVOY_LOG_MISC(debug, "sendmsg failed with error code {}",
                 static_cast<int>(quic_result.error_code));
  return Api::IoCallUint64Result(
      /*rc=*/0,
      /*err=*/Api::IoErrorPtr(new Network::IoSocketError(quic_result.error_code),
                              Network::IoSocketError::deleteIoError));
}

uint64_t UdpGsoBatchWriter::getMaxPacketSize(const Network::Address::Instance& peer_address) const {
  // TODO(yugant): Is there a better way to create it? Use common function that uses Instance.
  Network::Address::InstanceConstSharedPtr enpr =
      Network::Address::InstanceConstSharedPtr(&peer_address);
  quic::QuicSocketAddress peer_addr = envoyAddressInstanceToQuicSocketAddress(enpr);
  return static_cast<uint64_t>(GetMaxPacketSize(peer_addr));
}

char* UdpGsoBatchWriter::getNextWriteLocation(const Network::Address::Ip* local_ip,
                                              const Network::Address::Instance& peer_address) {
  // TODO(yugant): Is there a better way to create it? Use common function that uses Instance.
  Network::Address::InstanceConstSharedPtr enpr =
      Network::Address::InstanceConstSharedPtr(&peer_address);
  quic::QuicSocketAddress peer_addr = envoyAddressInstanceToQuicSocketAddress(enpr);
  quic::QuicIpAddress self_ip = envoyAddressIpToQuicIpAddress(local_ip);
  quic::QuicPacketBuffer quic_buf = GetNextWriteLocation(self_ip, peer_addr);
  return static_cast<char*>(quic_buf.buffer);
}

Api::IoCallUint64Result UdpGsoBatchWriter::flush() {
  quic::WriteResult quic_result = Flush();

  // TODO(yugant): Later have all this code below into a single helper function
  // for both WritePacket and Flush implementations
  if (quic_result.status == quic::WRITE_STATUS_OK) {
    // Write Successful
    if (quic_result.bytes_written == 0) {
      ENVOY_LOG_MISC(trace, "sendmsg successful, message buffered to send");
    } else {
      ENVOY_LOG_MISC(trace, "sendmsg successful, flushed bytes {}", quic_result.bytes_written);
    }
    // Return bytes_written as rc & nullptr as error on success
    return Api::IoCallUint64Result(
        /*rc=*/quic_result.bytes_written,
        /*err=*/Api::IoErrorPtr(nullptr, Network::IoSocketError::deleteIoError));
  }

  if (quic_result.status == quic::WRITE_STATUS_BLOCKED_DATA_BUFFERED) {
    // Data was buffered, no need to return error
    ENVOY_LOG_MISC(trace, "sendmsg blocked, message buffered to send");
    return Api::IoCallUint64Result(
        /*rc=*/0,
        /*err=*/Api::IoErrorPtr(nullptr, Network::IoSocketError::deleteIoError));
  }

  if (quic_result.status == quic::WRITE_STATUS_BLOCKED) {
    ENVOY_LOG_MISC(trace, "sendmsg blocked, message not buffered");
    return Api::IoCallUint64Result(
        /*rc=*/0,
        /*err=*/Api::IoErrorPtr(new Network::IoSocketError(quic_result.error_code),
                                Network::IoSocketError::deleteIoError));
  }

  // Write Failed, return {0 and error_code}
  ENVOY_LOG_MISC(debug, "sendmsg failed with error code {}",
                 static_cast<int>(quic_result.error_code));
  return Api::IoCallUint64Result(
      /*rc=*/0,
      /*err=*/Api::IoErrorPtr(new Network::IoSocketError(quic_result.error_code),
                              Network::IoSocketError::deleteIoError));
}

UdpGsoBatchWriterFactory::UdpGsoBatchWriterFactory() {}

Network::UdpPacketWriterPtr
UdpGsoBatchWriterFactory::createUdpPacketWriter(Network::Socket& socket) {
  // Keep It Simple for now
  return std::make_unique<UdpGsoBatchWriter>(socket);
}

} // namespace Quic
} // namespace Envoy