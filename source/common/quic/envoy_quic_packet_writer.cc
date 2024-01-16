#include "source/common/quic/envoy_quic_packet_writer.h"

#include <memory>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/quic/envoy_quic_utils.h"

namespace Envoy {
namespace Quic {

namespace {

quic::WriteResult convertToQuicWriteResult(Api::IoCallUint64Result& result) {
  if (result.ok()) {
    return {quic::WRITE_STATUS_OK, static_cast<int>(result.return_value_)};
  }
  quic::WriteStatus status = result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again
                                 ? quic::WRITE_STATUS_BLOCKED
                                 : quic::WRITE_STATUS_ERROR;
  return {status, static_cast<int>(result.err_->getSystemErrorCode())};
}

} // namespace

EnvoyQuicPacketWriter::EnvoyQuicPacketWriter(Network::UdpPacketWriterPtr envoy_udp_packet_writer)
    : envoy_udp_packet_writer_(std::move(envoy_udp_packet_writer)) {}

quic::WriteResult EnvoyQuicPacketWriter::WritePacket(
    const char* buffer, size_t buffer_len, const quic::QuicIpAddress& self_ip,
    const quic::QuicSocketAddress& peer_address, quic::PerPacketOptions* options,
    [[maybe_unused]] const quic::QuicPacketWriterParams& params) {
  ASSERT(options == nullptr, "Per packet option is not supported yet.");

  Buffer::BufferFragmentImpl fragment(buffer, buffer_len, nullptr);
  Buffer::OwnedImpl buf;
  buf.addBufferFragment(fragment);

  quic::QuicSocketAddress self_address(self_ip, /*port=*/0);
  Network::Address::InstanceConstSharedPtr local_addr =
      quicAddressToEnvoyAddressInstance(self_address);
  Network::Address::InstanceConstSharedPtr remote_addr =
      quicAddressToEnvoyAddressInstance(peer_address);

  Api::IoCallUint64Result result = envoy_udp_packet_writer_->writePacket(
      buf, local_addr == nullptr ? nullptr : local_addr->ip(), *remote_addr);

  return convertToQuicWriteResult(result);
}

absl::optional<int> EnvoyQuicPacketWriter::MessageTooBigErrorCode() const { return EMSGSIZE; }

quic::QuicByteCount
EnvoyQuicPacketWriter::GetMaxPacketSize(const quic::QuicSocketAddress& peer_address) const {
  Network::Address::InstanceConstSharedPtr remote_addr =
      quicAddressToEnvoyAddressInstance(peer_address);
  return static_cast<quic::QuicByteCount>(envoy_udp_packet_writer_->getMaxPacketSize(*remote_addr));
}

quic::QuicPacketBuffer
EnvoyQuicPacketWriter::GetNextWriteLocation(const quic::QuicIpAddress& self_ip,
                                            const quic::QuicSocketAddress& peer_address) {
  quic::QuicSocketAddress self_address(self_ip, /*port=*/0);
  Network::Address::InstanceConstSharedPtr local_addr =
      quicAddressToEnvoyAddressInstance(self_address);
  Network::Address::InstanceConstSharedPtr remote_addr =
      quicAddressToEnvoyAddressInstance(peer_address);
  Network::UdpPacketWriterBuffer write_location = envoy_udp_packet_writer_->getNextWriteLocation(
      local_addr == nullptr ? nullptr : local_addr->ip(), *remote_addr);
  return {reinterpret_cast<char*>(write_location.buffer_), write_location.release_buffer_};
}

quic::WriteResult EnvoyQuicPacketWriter::Flush() {
  Api::IoCallUint64Result result = envoy_udp_packet_writer_->flush();
  return convertToQuicWriteResult(result);
}

} // namespace Quic
} // namespace Envoy
