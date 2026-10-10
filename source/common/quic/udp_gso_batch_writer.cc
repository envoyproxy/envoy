#include "source/common/quic/udp_gso_batch_writer.h"

#include "source/common/network/io_socket_error_impl.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Quic {
namespace {
Api::IoCallUint64Result convertQuicWriteResult(quic::WriteResult quic_result, size_t payload_len) {
  switch (quic_result.status) {
  case quic::WRITE_STATUS_OK:
    if (quic_result.bytes_written == 0) {
      ENVOY_LOG_MISC(trace, "sendmsg successful, message buffered to send");
    } else {
      ENVOY_LOG_MISC(trace, "sendmsg successful, flushed bytes {}", quic_result.bytes_written);
    }
    // Return payload_len as rc & nullptr as error on success
    return {/*rc=*/payload_len,
            /*err=*/Api::IoError::none()};
  case quic::WRITE_STATUS_BLOCKED_DATA_BUFFERED:
    // Data was buffered, Return payload_len as rc & nullptr as error
    ENVOY_LOG_MISC(trace, "sendmsg blocked, message buffered to send");
    return {/*rc=*/payload_len,
            /*err=*/Api::IoError::none()};
  case quic::WRITE_STATUS_BLOCKED:
    // Writer blocked, return error
    ENVOY_LOG_MISC(trace, "sendmsg blocked, message not buffered");
    return {/*rc=*/0,
            /*err=*/Network::IoSocketError::getIoSocketEagainError()};
  default:
    // Write Failed, return {0 and error_code}
    ENVOY_LOG_MISC(trace, "sendmsg failed with error code {}",
                   static_cast<int>(quic_result.error_code));
    return {/*rc=*/0,
            /*err=*/Network::IoSocketError::create(quic_result.error_code)};
  }
}

} // namespace

// Initialize QuicGsoBatchWriter, set io_handle_ and stats_
UdpGsoBatchWriter::UdpGsoBatchWriter(Network::IoHandle& io_handle, Stats::Scope& scope)
    : quic::QuicGsoBatchWriter(io_handle.fdDoNotUse()), stats_(generateStats(scope)),
      default_writer_(io_handle) {}

UdpGsoBatchWriter::~UdpGsoBatchWriter() { stats_.internal_buffer_size_.sub(buffered_bytes_); }

Api::IoCallUint64Result
UdpGsoBatchWriter::writePacket(const Buffer::Instance& buffer, const Network::Address::Ip* local_ip,
                               const Network::Address::Instance& peer_address) {
  const size_t payload_len = static_cast<size_t>(buffer.length());

  if (payload_len == 0 &&
      !Runtime::runtimeFeatureEnabled("envoy.reloadable_features.udp_send_zero_length_datagrams")) {
    return Api::ioCallUint64ResultNoError();
  }

  if (isWriteBlocked()) {
    return {0, Network::IoSocketError::getIoSocketEagainError()};
  }

  // GSO requires contiguous payloads within its packet size limit. Empty datagrams must also be
  // sent separately. Flush older packets first to preserve datagram ordering in both cases.
  const bool use_default_writer =
      payload_len > getMaxPacketSize(peer_address) || buffer.getRawSlices(2).size() > 1;
  if ((payload_len == 0 || use_default_writer) && !buffered_writes().empty()) {
    quic::WriteResult flush_result = Flush();
    updateUdpGsoBatchWriterStats(flush_result);
    if (flush_result.status != quic::WRITE_STATUS_OK) {
      return convertQuicWriteResult(flush_result, /*payload_len=*/0);
    }
  }

  if (use_default_writer) {
    auto result = default_writer_.writePacket(buffer, local_ip, peer_address);
    if (result.ok()) {
      stats_.total_bytes_sent_.add(result.return_value_);
    }
    return result;
  }

  // Convert received parameters to relevant forms
  quic::QuicSocketAddress peer_addr = envoyIpAddressToQuicSocketAddress(peer_address.ip());
  quic::QuicSocketAddress self_addr = envoyIpAddressToQuicSocketAddress(local_ip);
  ASSERT(payload_len == 0 || buffer.getRawSlices().size() == 1);
  char empty_payload = 0;
  const char* payload =
      payload_len == 0 ? &empty_payload : static_cast<char*>(buffer.frontSlice().mem_);

  // TODO(yugant): Currently we do not use PerPacketOptions with Quic, we may want to
  // specify this parameter here at a later stage.
  quic::QuicPacketWriterParams params;
  quic::WriteResult quic_result = WritePacket(payload, payload_len, self_addr.host(), peer_addr,
                                              /*quic::PerPacketOptions=*/nullptr, params);
  updateUdpGsoBatchWriterStats(quic_result);

  return convertQuicWriteResult(quic_result, payload_len);
}

uint64_t UdpGsoBatchWriter::getMaxPacketSize(const Network::Address::Instance& peer_address) const {
  quic::QuicSocketAddress peer_addr = envoyIpAddressToQuicSocketAddress(peer_address.ip());
  return static_cast<uint64_t>(GetMaxPacketSize(peer_addr));
}

Network::UdpPacketWriterBuffer
UdpGsoBatchWriter::getNextWriteLocation(const Network::Address::Ip* local_ip,
                                        const Network::Address::Instance& peer_address) {
  quic::QuicSocketAddress peer_addr = envoyIpAddressToQuicSocketAddress(peer_address.ip());
  quic::QuicSocketAddress self_addr = envoyIpAddressToQuicSocketAddress(local_ip);
  quic::QuicPacketBuffer quic_buf = GetNextWriteLocation(self_addr.host(), peer_addr);
  return {reinterpret_cast<uint8_t*>(quic_buf.buffer), Network::UdpMaxOutgoingPacketSize,
          quic_buf.release_buffer};
}

Api::IoCallUint64Result UdpGsoBatchWriter::flush() {
  quic::WriteResult quic_result = Flush();
  updateUdpGsoBatchWriterStats(quic_result);

  return convertQuicWriteResult(quic_result, /*payload_len=*/0);
}

void UdpGsoBatchWriter::updateUdpGsoBatchWriterStats(quic::WriteResult quic_result) {
  if (quic_result.status == quic::WRITE_STATUS_OK && quic_result.bytes_written > 0) {
    if (gso_size_ > 0u) {
      uint64_t num_pkts_in_batch =
          std::ceil(static_cast<float>(quic_result.bytes_written) / gso_size_);
      stats_.pkts_sent_per_batch_.recordValue(num_pkts_in_batch);
    }
    stats_.total_bytes_sent_.add(quic_result.bytes_written);
  }
  const uint64_t buffered_bytes = batch_buffer().SizeInUse();
  if (buffered_bytes >= buffered_bytes_) {
    stats_.internal_buffer_size_.add(buffered_bytes - buffered_bytes_);
  } else {
    stats_.internal_buffer_size_.sub(buffered_bytes_ - buffered_bytes);
  }
  buffered_bytes_ = buffered_bytes;
  gso_size_ = buffered_writes().empty() ? 0u : buffered_writes().front().buf_len;
}

UdpGsoBatchWriterStats UdpGsoBatchWriter::generateStats(Stats::Scope& scope) {
  return {
      UDP_GSO_BATCH_WRITER_STATS(POOL_COUNTER(scope), POOL_GAUGE(scope), POOL_HISTOGRAM(scope))};
}

Network::UdpPacketWriterPtr
UdpGsoBatchWriterFactory::createUdpPacketWriter(Network::IoHandle& io_handle, Stats::Scope& scope,
                                                Envoy::Event::Dispatcher&,
                                                absl::AnyInvocable<void() &&>) {
  return std::make_unique<UdpGsoBatchWriter>(io_handle, scope);
}

} // namespace Quic
} // namespace Envoy
