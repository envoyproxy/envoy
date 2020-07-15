#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/socket.h"
#include "envoy/network/udp_packet_writer_handler.h"

#include "common/network/io_socket_error_impl.h"

namespace Envoy {
namespace Network {

class UdpDefaultWriter : public UdpPacketWriter {
public:
  UdpDefaultWriter(Network::Socket& socket);

  ~UdpDefaultWriter() override;

  // following writeToSocket() utilizes the Utility::writeToSocket implementation
  Api::IoCallUint64Result writeToSocket(const Buffer::Instance& buffer, const Address::Ip* local_ip,
                                        const Address::Instance& peer_address) override;

  bool isWriteBlocked() const override { return write_blocked_; }
  void setWritable() override { write_blocked_ = false; }
  uint64_t getMaxPacketSize(const Address::Instance& /*peer_address*/) const override {
    // TODO(yugant): Find a proper place to get this constant
    return 1452u;
  }
  bool isBatchMode() const override { return false; }

  char* getNextWriteLocation(const Address::Ip* /*local_ip*/,
                             const Address::Instance& /*peer_address*/) override {
    return nullptr;
  }
  Api::IoCallUint64Result flush() override {
    return Api::IoCallUint64Result(
        /*rc=*/0,
        /*err=*/Api::IoErrorPtr(nullptr, Network::IoSocketError::deleteIoError));
  }

  std::string name() const override;

private:
  bool write_blocked_;
  Network::Socket& socket_;
};

} // namespace Network
} // namespace Envoy