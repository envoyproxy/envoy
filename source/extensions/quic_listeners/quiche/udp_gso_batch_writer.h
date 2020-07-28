#pragma once

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
// QUICHE allows ignored qualifiers
#pragma GCC diagnostic ignored "-Wignored-qualifiers"

// QUICHE doesn't mark override at QuicBatchWriterBase::SupportsReleaseTime()
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winconsistent-missing-override"
#elif defined(__GNUC__) && __GNUC__ >= 5
#pragma GCC diagnostic ignored "-Wsuggest-override"
#endif

#include "quiche/quic/core/batch_writer/quic_gso_batch_writer.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#pragma GCC diagnostic pop

#include "envoy/network/udp_packet_writer_handler.h"

#include "common/protobuf/utility.h"
#include "common/runtime/runtime_protos.h"

namespace Envoy {
namespace Quic {

/**
 * UdpPacketWriter implementation based on quic::QuicGsoBatchWriter to send packets
 * in batches, using UDP socket's generic segmentation offload(GSO) capability.
 */
class UdpGsoBatchWriter : public quic::QuicGsoBatchWriter, public Network::UdpPacketWriter {
public:
  UdpGsoBatchWriter(Network::IoHandle& io_handle, Stats::Scope& scope);

  ~UdpGsoBatchWriter() override;

  // writePacket perform batched sends based on QuicGsoBatchWriter::WritePacket
  Api::IoCallUint64Result writePacket(const Buffer::Instance& buffer,
                                      const Network::Address::Ip* local_ip,
                                      const Network::Address::Instance& peer_address) override;

  // UdpPacketWriter Implementations
  bool isWriteBlocked() const override { return IsWriteBlocked(); }
  void setWritable() override { return SetWritable(); }
  bool isBatchMode() const override { return IsBatchMode(); }
  uint64_t getMaxPacketSize(const Network::Address::Instance& peer_address) const override;
  Network::UdpPacketWriterBuffer
  getNextWriteLocation(const Network::Address::Ip* local_ip,
                       const Network::Address::Instance& peer_address) override;
  Api::IoCallUint64Result flush() override;

  /**
   * @brief Update stats_ field for the udp packet writer
   * @param quic_result is the result from Flush/WritePacket
   */
  void updateUdpPacketWriterStats(quic::WriteResult quic_result);

  /**
   * @brief Generate UdpPacketWriterStats object from scope
   * @param scope for stats
   * @return UdpPacketWriterStats for scope
   */
  Network::UdpPacketWriterStats generateStats(Stats::Scope& scope);

private:
  Network::UdpPacketWriterStats stats_;
};

class UdpGsoBatchWriterFactory : public Network::UdpPacketWriterFactory {
public:
  UdpGsoBatchWriterFactory();

  Network::UdpPacketWriterPtr createUdpPacketWriter(Network::IoHandle& io_handle,
                                                    Stats::Scope& scope) override;

private:
  envoy::config::core::v3::RuntimeFeatureFlag enabled_;
};

} // namespace Quic
} // namespace Envoy
