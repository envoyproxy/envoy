#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/socket.h"
#include "envoy/network/udp_packet_writer_handler.h"

#include "source/common/network/io_socket_error_impl.h"

namespace Envoy {
namespace Network {

class UdpDefaultWriter : public UdpPacketWriter {
public:
  UdpDefaultWriter(Network::IoHandle& io_handle);

  ~UdpDefaultWriter() override;

  // Following writePacket utilizes Utility::writeToSocket() implementation
  Api::IoCallUint64Result writePacket(const Buffer::Instance& buffer, const Address::Ip* local_ip,
                                      const Address::Instance& peer_address) override;

  bool isWriteBlocked() const override { return write_blocked_; }
  void setWritable() override { write_blocked_ = false; }
  uint64_t getMaxPacketSize(const Address::Instance& /*peer_address*/) const override {
    return Network::UdpMaxOutgoingPacketSize;
  }
  bool isBatchMode() const override { return false; }
  Network::UdpPacketWriterBuffer
  getNextWriteLocation(const Address::Ip* /*local_ip*/,
                       const Address::Instance& /*peer_address*/) override {
    return {nullptr, 0, nullptr};
  }
  Api::IoCallUint64Result flush() override {
    return {/*rc=*/0,
            /*err=*/Api::IoError::none()};
  }

private:
  bool write_blocked_{false};
  Network::IoHandle& io_handle_;
};

class UdpDefaultWriterFactory : public Network::UdpPacketWriterFactory {
public:
  Network::UdpPacketWriterPtr createUdpPacketWriter(Network::IoHandle& io_handle,
                                                    Stats::Scope&) override {
    return std::make_unique<UdpDefaultWriter>(io_handle);
  }
};

} // namespace Network
} // namespace Envoy
