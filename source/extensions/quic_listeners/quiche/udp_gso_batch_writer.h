#pragma once

#if !defined(__linux__)
#define UDP_GSO_BATCH_WRITER_COMPILETIME_SUPPORT 0
#else
#define UDP_GSO_BATCH_WRITER_COMPILETIME_SUPPORT 1

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
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
 * @brief The following can be used to collect statistics
 * related to UdpGsoBatchWriter. The stats maintained are
 * as follows:
 *
 * @total_bytes_sent: Maintains the count of total bytes
 * sent via the UdpGsoBatchWriter on the current ioHandle
 * via both WritePacket() and Flush() functions.
 *
 * @internal_buffer_size: Gauge value to keep a track of the
 * total bytes buffered to writer by UdpGsoBatchWriter.
 * Resets whenever the internal bytes are sent to the client.
 *
 * @pkts_sent_per_batch: Histogram to keep maintain stats of
 * total number of packets sent in each batch by UdpGsoBatchWriter
 * Provides summary count of batch-sizes within bucketed range,
 * and also provides sum and count stats.
 *
 * TODO(danzh): Add writer stats to QUIC Documentation when it is
 * created for QUIC/HTTP3 docs. Also specify in the documentation
 * that user has to compile in QUICHE to use UdpGsoBatchWriter.
 */
#define UDP_GSO_BATCH_WRITER_STATS(COUNTER, GAUGE, HISTOGRAM)                                      \
  COUNTER(total_bytes_sent)                                                                        \
  GAUGE(internal_buffer_size, NeverImport)                                                         \
  HISTOGRAM(pkts_sent_per_batch, Unspecified)

/**
 * Wrapper struct for udp gso batch writer stats. @see stats_macros.h
 */
struct UdpGsoBatchWriterStats {
  UDP_GSO_BATCH_WRITER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT,
                             GENERATE_HISTOGRAM_STRUCT)
};

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

private:
  /**
   * @brief Update stats_ field for the udp packet writer
   * @param quic_result is the result from Flush/WritePacket
   */
  void updateUdpGsoBatchWriterStats(quic::WriteResult quic_result);

  /**
   * @brief Generate UdpGsoBatchWriterStats object from scope
   * @param scope for stats
   * @return UdpGsoBatchWriterStats for scope
   */
  UdpGsoBatchWriterStats generateStats(Stats::Scope& scope);
  UdpGsoBatchWriterStats stats_;
  uint64_t gso_size_;
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

#endif // defined(__linux__)
