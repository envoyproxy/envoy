#pragma once

#include "envoy/network/udp_packet_writer_handler.h"

#include "quiche/quic/core/quic_packet_writer.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicPacketWriter : public quic::QuicPacketWriter {
public:
  EnvoyQuicPacketWriter(Network::UdpPacketWriterPtr envoy_udp_packet_writer);

  quic::WriteResult WritePacket(const char* buffer, size_t buf_len,
                                const quic::QuicIpAddress& self_address,
                                const quic::QuicSocketAddress& peer_address,
                                quic::PerPacketOptions* options,
                                const quic::QuicPacketWriterParams& params) override;

  // quic::QuicPacketWriter
  bool IsWriteBlocked() const override { return envoy_udp_packet_writer_->isWriteBlocked(); }
  void SetWritable() override { envoy_udp_packet_writer_->setWritable(); }
  bool IsBatchMode() const override { return envoy_udp_packet_writer_->isBatchMode(); }
  // Currently this writer doesn't support pacing offload.
  bool SupportsReleaseTime() const override { return false; }
  // Currently this writer doesn't support Explicit Congestion Notification.
  bool SupportsEcn() const override { return false; }

  absl::optional<int> MessageTooBigErrorCode() const override;
  quic::QuicByteCount GetMaxPacketSize(const quic::QuicSocketAddress& peer_address) const override;
  quic::QuicPacketBuffer GetNextWriteLocation(const quic::QuicIpAddress& self_address,
                                              const quic::QuicSocketAddress& peer_address) override;
  quic::WriteResult Flush() override;

private:
  Network::UdpPacketWriterPtr envoy_udp_packet_writer_;
};

using QuicPacketWriterPtr = std::unique_ptr<quic::QuicPacketWriter>;

class QuicPacketWriterFactory : public Network::UdpPacketWriterFactory {
 public:
  ~QuicPacketWriterFactory() override = default;

  virtual QuicPacketWriterPtr createQuicPacketWriter(
      Network::IoHandle& io_handle, Stats::Scope& scope,
      Envoy::Event::Dispatcher& dispatcher,
      absl::AnyInvocable<void() &&> on_can_write_cb) PURE;

  Network::UdpPacketWriterPtr createUdpPacketWriter(
      Network::IoHandle&, Stats::Scope&, Envoy::Event::Dispatcher&,
      absl::AnyInvocable<void() &&>) override {
    IS_ENVOY_BUG("createUdpPacketWriter called on QuicPacketWriterFactory");
    return nullptr;
  }
};

using QuicPacketWriterFactoryPtr = std::unique_ptr<QuicPacketWriterFactory>;

} // namespace Quic
} // namespace Envoy
