#include "extensions/quic_listeners/quiche/envoy_quic_packet_writer.h"

#pragma GCC diagnostic push

// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/quic_types.h"

#pragma GCC diagnostic pop

#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"
#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Quic {

quic::WriteResult EnvoyQuicPacketWriter::WritePacket(const char* buffer, size_t buf_len,
                                                     const quic::QuicIpAddress& self_ip,
                                                     const quic::QuicSocketAddress& peer_address,
                                                     quic::PerPacketOptions* options) {
  ASSERT(options == nullptr, "Per packet option is not supported yet.");
  ASSERT(!write_blocked_, "Cannot write while IO handle is blocked.");

  Buffer::BufferFragmentImpl fragment(buffer, buf_len, nullptr);
  Buffer::OwnedImpl buffer_wrapper;
  buffer_wrapper.addBufferFragment(fragment);
  quic::QuicSocketAddress self_address(self_ip, /*port=*/0);
  Network::Address::InstanceConstSharedPtr local_addr =
      quicAddressToEnvoyAddressInstance(self_address);
  Network::Address::InstanceConstSharedPtr remote_addr =
      quicAddressToEnvoyAddressInstance(peer_address);
  Network::UdpSendData send_data{local_addr == nullptr ? nullptr : local_addr->ip(), *remote_addr,
                                 buffer_wrapper};
  Api::IoCallUint64Result result = listener_.send(send_data);
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
