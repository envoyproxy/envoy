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
  if (isWriteBlocked()) {
    // Writer Blocked, return EAGAIN
    ENVOY_LOG_MISC(trace, "Udp Writer is blocked, skip sending");
    return Api::IoCallUint64Result(
        /*rc=*/0,
        /*err=*/Api::IoErrorPtr(new Network::IoSocketError(SOCKET_ERROR_AGAIN),
                                Network::IoSocketError::deleteIoError));
  }

  Api::IoCallUint64Result result =
      Utility::writeToSocket(io_handle_, buffer, local_ip, peer_address);
  if (result.ok()) {
    // Update UdpPacketWriter Stats
    stats_.total_bytes_sent_.add(result.rc_);
  } else if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
    // Writer is blocked when error code received is EWOULDBLOCK/EAGAIN
    write_blocked_ = true;
  }
  return result;
}

Network::UdpDefaultWriterStats UdpDefaultWriter::generateStats(Stats::Scope& scope) {
  return {UDP_DEFAULT_WRITER_STATS(POOL_COUNTER(scope))};
}

} // namespace Network
} // namespace Envoy
