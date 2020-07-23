#include "common/network/udp_packet_writer_handler_impl.h"

#include "common/buffer/buffer_impl.h"
#include "common/network/utility.h"
#include "common/network/well_known_names.h"

namespace Envoy {
namespace Network {

UdpDefaultWriter::UdpDefaultWriter(Network::IoHandle& io_handle, Stats::Scope& scope)
    : write_blocked_(false), io_handle_(io_handle), stats_(generateStats(scope)) {}

UdpDefaultWriter::~UdpDefaultWriter() = default;

Api::IoCallUint64Result UdpDefaultWriter::writePacket(const Buffer::Instance& buffer,
                                                      const Address::Ip* local_ip,
                                                      const Address::Instance& peer_address) {
  if (!isWriteBlocked()) {
    Api::IoCallUint64Result result =
        Utility::writeToSocket(io_handle_, buffer, local_ip, peer_address);
    if (result.err_ && result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
      // Writer is blocked when error code received is EWOULDBLOCK/EAGAIN
      write_blocked_ = true;
    }
    // Update UdpPacketWriter Stats
    stats_.sent_bytes_.set(result.rc_);
    return result;
  }
  // Otherwise Writer Blocked, return EAGAIN
  ENVOY_LOG_MISC(trace, "Udp Writer is blocked, skip sending");
  return Api::IoCallUint64Result(
      /*rc=*/0,
      /*err=*/Api::IoErrorPtr(new Network::IoSocketError(SOCKET_ERROR_AGAIN),
                              Network::IoSocketError::deleteIoError));
}

std::string UdpDefaultWriter::name() const { return UdpWriterNames::get().DefaultWriter; }

Network::UdpPacketWriterStats UdpDefaultWriter::generateStats(Stats::Scope& scope) {
  return {UDP_PACKET_WRITER_STATS(POOL_GAUGE(scope))};
}

Network::IoHandle& UdpDefaultWriter::getWriterIoHandle() const { return io_handle_; }

} // namespace Network
} // namespace Envoy
