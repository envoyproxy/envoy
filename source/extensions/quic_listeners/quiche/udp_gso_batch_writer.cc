#include "extensions/quic_listeners/quiche/udp_gso_batch_writer.h"

#include "common/network/io_socket_error_impl.h"

#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"

namespace Envoy {
namespace Quic {

namespace {
Api::IoCallUint64Result convertQuicWriteResult(quic::WriteResult quic_result) {
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

} // namespace

// Initialize QuicGsoBatchWriter, set io_handle_ and stats_
UdpGsoBatchWriter::UdpGsoBatchWriter(Network::IoHandle& io_handle, Stats::Scope& scope)
    : quic::QuicGsoBatchWriter(std::make_unique<quic::QuicBatchWriterBuffer>(), io_handle.fd()),
      io_handle_(io_handle), stats_(generateStats(scope)) {}

// Do Nothing in the Destructor For now
UdpGsoBatchWriter::~UdpGsoBatchWriter() {}

Api::IoCallUint64Result
UdpGsoBatchWriter::writePacket(const Buffer::Instance& buffer, const Network::Address::Ip* local_ip,
                               const Network::Address::Instance& peer_address) {
  // Convert received parameters to relevant forms
  quic::QuicSocketAddress peer_addr = envoyAddressInstanceToQuicSocketAddress(peer_address);
  quic::QuicSocketAddress self_addr = envoyAddressIpToQuicSocketAddress(local_ip);
  size_t payload_len = static_cast<size_t>(buffer.length());

  // TODO(yugant): If QUIC Then: define PerPacketOptions
  // Take extra parameter to writeToSocket and use it to create PerPacketOptions
  // Also we are not taking care of setting write_blocked_ (if needed) over here, as the
  // WritePacket implementation will do that for us.
  quic::WriteResult quic_result =
      WritePacket(buffer.toString().c_str(), payload_len, self_addr.host(), peer_addr,
                  /*quic::PerPacketOptions=*/nullptr);
  updateUdpPacketWriterStats(quic_result, payload_len);

  return convertQuicWriteResult(quic_result);
}

uint64_t UdpGsoBatchWriter::getMaxPacketSize(const Network::Address::Instance& peer_address) const {
  quic::QuicSocketAddress peer_addr = envoyAddressInstanceToQuicSocketAddress(peer_address);
  return static_cast<uint64_t>(GetMaxPacketSize(peer_addr));
}

char* UdpGsoBatchWriter::getNextWriteLocation(const Network::Address::Ip* local_ip,
                                              const Network::Address::Instance& peer_address) {
  quic::QuicSocketAddress peer_addr = envoyAddressInstanceToQuicSocketAddress(peer_address);
  quic::QuicSocketAddress self_addr = envoyAddressIpToQuicSocketAddress(local_ip);
  quic::QuicPacketBuffer quic_buf = GetNextWriteLocation(self_addr.host(), peer_addr);
  return static_cast<char*>(quic_buf.buffer);
}

Api::IoCallUint64Result UdpGsoBatchWriter::flush() {
  quic::WriteResult quic_result = Flush();
  updateUdpPacketWriterStats(quic_result, /*payload_len=*/0);

  return convertQuicWriteResult(quic_result);
}

void UdpGsoBatchWriter::updateUdpPacketWriterStats(quic::WriteResult quic_result,
                                                   size_t payload_len) {
  stats_.internal_buffer_size_.set(batch_buffer().SizeInUse());
  stats_.sent_bytes_.set(quic_result.bytes_written);
  if (quic_result.bytes_written > 0) {
    stats_.last_buffered_msg_size_.set(
        payload_len < stats_.last_buffered_msg_size_.value() ? 0 : payload_len);
  } else if (quic_result.status == quic::WRITE_STATUS_OK ||
             quic_result.status == quic::WRITE_STATUS_BLOCKED_DATA_BUFFERED) {
    stats_.last_buffered_msg_size_.set(payload_len);
  } else {
    stats_.last_buffered_msg_size_.set(0);
  }
}

Network::UdpPacketWriterStats UdpGsoBatchWriter::generateStats(Stats::Scope& scope) {
  return {UDP_PACKET_WRITER_STATS(POOL_GAUGE(scope))};
}

UdpGsoBatchWriterFactory::UdpGsoBatchWriterFactory() {}

Network::UdpPacketWriterPtr
UdpGsoBatchWriterFactory::createUdpPacketWriter(Network::IoHandle& io_handle, Stats::Scope& scope) {
  // Keep It Simple for now
  return std::make_unique<UdpGsoBatchWriter>(io_handle, scope);
}

} // namespace Quic
} // namespace Envoy