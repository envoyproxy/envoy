#pragma once

#include <optional>

#include "quiche/quic/core/quic_packet_writer.h"

namespace Envoy {
namespace Quic {

class FakeQuicPacketWriter : public quic::QuicPacketWriter {
public:
  FakeQuicPacketWriter() = default;
  ~FakeQuicPacketWriter() override = default;

  // quic::QuicPacketWriter
  quic::WriteResult WritePacket(const char*, size_t, const quic::QuicIpAddress&,
                                const quic::QuicSocketAddress&, quic::PerPacketOptions*,
                                const quic::QuicPacketWriterParams&) override {
    return quic::WriteResult(quic::WRITE_STATUS_OK, 0);
  }
  bool IsWriteBlocked() const override { return false; }
  void SetWritable() override {}
  quic::QuicByteCount GetMaxPacketSize(const quic::QuicSocketAddress&) const override {
    return 1500;
  }
  bool SupportsReleaseTime() const override { return false; }
  bool IsBatchMode() const override { return false; }
  quic::QuicPacketBuffer GetNextWriteLocation(const quic::QuicIpAddress&,
                                              const quic::QuicSocketAddress&) override {
    return {nullptr, nullptr};
  }
  quic::WriteResult Flush() override { return quic::WriteResult(quic::WRITE_STATUS_OK, 0); }
  std::optional<int> MessageTooBigErrorCode() const override { return std::nullopt; }
  bool SupportsEcn() const override { return false; }
};

} // namespace Quic
} // namespace Envoy
