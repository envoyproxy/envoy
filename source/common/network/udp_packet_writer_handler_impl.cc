#include "common/network/udp_packet_writer_handler_impl.h"

#include "common/buffer/buffer_impl.h"
#include "common/network/utility.h"
#include "common/network/well_known_names.h"

namespace Envoy {
namespace Network {

UdpDefaultWriter::UdpDefaultWriter(Network::Socket& socket)
    : write_blocked_(false), socket_(socket) {
  // Just set the socket and write_blocked_ in the constructor
}

// Destructor does nothing for now
UdpDefaultWriter::~UdpDefaultWriter() {}

Api::IoCallUint64Result UdpDefaultWriter::writeToSocket(const Buffer::Instance& buffer,
                                                        const Address::Ip* local_ip,
                                                        const Address::Instance& peer_address) {
  // Simple call to Utility::writeToSocket
  if (!isWriteBlocked()) {
    Api::IoCallUint64Result result =
        Utility::writeToSocket(socket_.ioHandle(), buffer, local_ip, peer_address);

    if (result.err_ && result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
      // Writer is blocked when error code received is EWOULDBLOCK/EAGAIN
      write_blocked_ = true;
    }

    return result;
  }
  // Otherwise Return Blocked
  // TODO(yugant) see a better way to create EAGAIN error here
  ENVOY_LOG_MISC(trace, "Udp Writer is blocked, skip sending");
  return Api::IoCallUint64Result(/*rc=*/0,
                                 /*err=*/Api::IoErrorPtr(new Network::IoSocketError(EAGAIN),
                                                         Network::IoSocketError::deleteIoError));
}

std::string UdpDefaultWriter::name() const { return UdpWriterNames::get().DefaultWriter; }

} // namespace Network
} // namespace Envoy