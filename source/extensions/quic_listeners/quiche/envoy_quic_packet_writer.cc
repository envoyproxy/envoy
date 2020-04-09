#include "extensions/quic_listeners/quiche/envoy_quic_packet_writer.h"

#include "common/buffer/buffer_impl.h"
#include "common/network/utility.h"

#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"

namespace Envoy {
namespace Quic {
EnvoyQuicPacketWriter::EnvoyQuicPacketWriter(Network::Socket& socket)
    : write_blocked_(false), socket_(socket) {}

quic::WriteResult EnvoyQuicPacketWriter::WritePacket(const char* buffer, size_t buf_len,
                                                     const quic::QuicIpAddress& self_ip,
                                                     const quic::QuicSocketAddress& peer_address,
                                                     quic::PerPacketOptions* options) {
  ASSERT(options == nullptr, "Per packet option is not supported yet.");
  ASSERT(!write_blocked_, "Cannot write while IO handle is blocked.");

  Buffer::RawSlice slice;
  slice.mem_ = const_cast<char*>(buffer);
  slice.len_ = buf_len;
  quic::QuicSocketAddress self_address(self_ip, /*port=*/0);
  Network::Address::InstanceConstSharedPtr local_addr =
      quicAddressToEnvoyAddressInstance(self_address);
  Network::Address::InstanceConstSharedPtr remote_addr =
      quicAddressToEnvoyAddressInstance(peer_address);
  Api::IoCallUint64Result result = Network::Utility::writeToSocket(
      socket_.ioHandle(), &slice, 1, local_addr == nullptr ? nullptr : local_addr->ip(),
      *remote_addr);
  if (result.ok()) {
    return {quic::WRITE_STATUS_OK, static_cast<int>(result.rc_)};
  }
  quic::WriteStatus status = result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again
                                 ? quic::WRITE_STATUS_BLOCKED
                                 : quic::WRITE_STATUS_ERROR;
  if (quic::IsWriteBlockedStatus(status)) {
    write_blocked_ = true;
  }
  return {status, static_cast<int>(result.err_->getErrorCode())};
}

} // namespace Quic
} // namespace Envoy
