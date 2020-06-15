#pragma once

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/quic_packet_writer.h"

#pragma GCC diagnostic pop

#include "envoy/network/listener.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicPacketWriter : public quic::QuicPacketWriter {
public:
  EnvoyQuicPacketWriter(Network::Socket& socket);

  quic::WriteResult WritePacket(const char* buffer, size_t buf_len,
                                const quic::QuicIpAddress& self_address,
                                const quic::QuicSocketAddress& peer_address,
                                quic::PerPacketOptions* options) override;

  // quic::QuicPacketWriter
  bool IsWriteBlocked() const override { return write_blocked_; }
  void SetWritable() override { write_blocked_ = false; }
  quic::QuicByteCount
  GetMaxPacketSize(const quic::QuicSocketAddress& /*peer_address*/) const override {
    return quic::kMaxOutgoingPacketSize;
  }
  // Currently this writer doesn't support pacing offload or batch writing.
  bool SupportsReleaseTime() const override { return false; }
  bool IsBatchMode() const override { return false; }
  quic::QuicPacketBuffer
  GetNextWriteLocation(const quic::QuicIpAddress& /*self_address*/,
                       const quic::QuicSocketAddress& /*peer_address*/) override {
    return {nullptr, nullptr};
  }
  quic::WriteResult Flush() override { return {quic::WRITE_STATUS_OK, 0}; }

private:
  // Modified by WritePacket() to indicate underlying IoHandle status.
  bool write_blocked_;
  Network::Socket& socket_;
};

} // namespace Quic
} // namespace Envoy
