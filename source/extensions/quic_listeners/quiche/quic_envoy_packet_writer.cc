#include "extensions/quic_listeners/quiche/quic_envoy_packet_writer.h"

#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"

namespace Envoy {
namespace Quic {
QuicEnvoyPacketWriter::QuicEnvoyPacketWriter(Network::UdpPacketWriter& envoy_udp_packet_writer)
    : envoy_udp_packet_writer_(envoy_udp_packet_writer) {}

quic::WriteResult QuicEnvoyPacketWriter::WritePacket(const char* buffer, size_t buf_len,
                                                     const quic::QuicIpAddress& self_ip,
                                                     const quic::QuicSocketAddress& peer_address,
                                                     quic::PerPacketOptions* options) {
  ASSERT(options == nullptr, "Per packet option is not supported yet.");

  Buffer::InstancePtr buf;
  buf = std::make_unique<Buffer::OwnedImpl>(buffer, buf_len);

  quic::QuicSocketAddress self_address(self_ip, /*port=*/0);
  Network::Address::InstanceConstSharedPtr local_addr =
      quicAddressToEnvoyAddressInstance(self_address);
  Network::Address::InstanceConstSharedPtr remote_addr =
      quicAddressToEnvoyAddressInstance(peer_address);

  Api::IoCallUint64Result result = envoy_udp_packet_writer_.writePacket(
      *buf, local_addr == nullptr ? nullptr : local_addr->ip(), *remote_addr);

  if (result.ok()) {
    return {quic::WRITE_STATUS_OK, static_cast<int>(result.rc_)};
  }
  quic::WriteStatus status = result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again
                                 ? quic::WRITE_STATUS_BLOCKED
                                 : quic::WRITE_STATUS_ERROR;
  return {status, static_cast<int>(result.err_->getErrorCode())};
}

quic::QuicByteCount
QuicEnvoyPacketWriter::GetMaxPacketSize(const quic::QuicSocketAddress& peer_address) const {
  Network::Address::InstanceConstSharedPtr remote_addr =
      quicAddressToEnvoyAddressInstance(peer_address);
  return static_cast<quic::QuicByteCount>(envoy_udp_packet_writer_.getMaxPacketSize(*remote_addr));
}

quic::QuicPacketBuffer
QuicEnvoyPacketWriter::GetNextWriteLocation(const quic::QuicIpAddress& self_ip,
                                            const quic::QuicSocketAddress& peer_address) {
  quic::QuicSocketAddress self_address(self_ip, /*port=*/0);
  Network::Address::InstanceConstSharedPtr local_addr =
      quicAddressToEnvoyAddressInstance(self_address);
  Network::Address::InstanceConstSharedPtr remote_addr =
      quicAddressToEnvoyAddressInstance(peer_address);
  // TODO(yugant):Change this to not only take a char* pointer here
  char* buf = envoy_udp_packet_writer_.getNextWriteLocation(
      local_addr == nullptr ? nullptr : local_addr->ip(), *remote_addr);
  return quic::QuicPacketBuffer(buf, nullptr);
}

quic::WriteResult QuicEnvoyPacketWriter::Flush() {
  Api::IoCallUint64Result result = envoy_udp_packet_writer_.flush();

  // TODO(yugant):Move the code below maybe to a single helper function
  if (result.ok()) {
    return {quic::WRITE_STATUS_OK, static_cast<int>(result.rc_)};
  }
  quic::WriteStatus status = result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again
                                 ? quic::WRITE_STATUS_BLOCKED
                                 : quic::WRITE_STATUS_ERROR;
  return {status, static_cast<int>(result.err_->getErrorCode())};
}

} // namespace Quic
} // namespace Envoy