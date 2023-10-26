#include "source/common/network/udp_packet_writer_handler_impl.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/utility.h"

namespace Envoy {
namespace Network {

UdpDefaultWriter::UdpDefaultWriter(Network::IoHandle& io_handle) : io_handle_(io_handle) {}

UdpDefaultWriter::~UdpDefaultWriter() = default;

Api::IoCallUint64Result UdpDefaultWriter::writePacket(const Buffer::Instance& buffer,
                                                      const Address::Ip* local_ip,
                                                      const Address::Instance& peer_address) {
  ASSERT(!write_blocked_, "Cannot write while IO handle is blocked.");
  Api::IoCallUint64Result result =
      Utility::writeToSocket(io_handle_, buffer, local_ip, peer_address);
  if (result.err_ && result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
    // Writer is blocked when error code received is EWOULDBLOCK/EAGAIN
    write_blocked_ = true;
  }
  return result;
}

} // namespace Network
} // namespace Envoy
