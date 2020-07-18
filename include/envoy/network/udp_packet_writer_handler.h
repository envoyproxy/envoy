#pragma once

#include <cstdint>
#include <memory>

#include "envoy/api/io_error.h"    // IoCallUint64Result
#include "envoy/buffer/buffer.h"   // Buffer
#include "envoy/network/address.h" // Address
#include "envoy/network/socket.h"  // Socket

namespace Envoy {
namespace Network {

class UdpPacketWriter {
public:
  virtual ~UdpPacketWriter() = default;

  /**
   * Sends a packet via given UDP socket with specific source address.
   * @param io_handle specify a io_handle to perform write on
   * @param buffer points to the buffer containing the packet
   * @param local_ip is the source address to be used to send.
   * @param peer_address is the destination address to send to.
   */
  virtual Api::IoCallUint64Result writeToSocket(Network::IoHandle& io_handle,
                                                const Buffer::Instance& buffer,
                                                const Address::Ip* local_ip,
                                                const Address::Instance& peer_address) PURE;

  // TODO(yugant): Change the comments below in proper format
  // Writes to the socket tied to the UdpPacket Writer
  virtual Api::IoCallUint64Result writePacket(const Buffer::Instance& buffer,
                                              const Address::Ip* local_ip,
                                              const Address::Instance& peer_address) PURE;

  // Returns true if the network socket is not writable.
  virtual bool isWriteBlocked() const PURE;

  // Records that the socket has become writable, for example when an EPOLLOUT
  // is received or an asynchronous write completes.
  virtual void setWritable() PURE;

  // Returns the maximum size of the packet which can be written using this
  // writer for the supplied peer address. This size may actually exceed the
  // size of a valid QUIC packet.
  virtual uint64_t getMaxPacketSize(const Address::Instance& peer_address) const PURE;

  // True=Batch mode. False=PassThrough mode.
  virtual bool isBatchMode() const PURE;

  // Returns pointer to the next write location in internal buffer,
  // it should be called iff the caller does not call writeToSocket
  // for the returned buffer.

  // TODO(yugant): Change char* return to a struct. Explanation below.
  //
  // Why not send len?
  // In UDP we do not care about the packet size as we have a contract stating that
  // each buffer will be of max len 1500-hdr-size
  //
  // Is it okay to skip the release_buffer function pointer?
  // For GsoBatchWriter YES, since it will always return nullptr for the function ptr
  // But other BatchWriter implementations may be using release_buffer fn ptr to
  // release buffers from a free_list. Hence for that it would be better to have the return
  // type here as a struct with both char* and function ptr.
  virtual char* getNextWriteLocation(const Address::Ip* local_ip,
                                     const Address::Instance& peer_address) PURE;

  // Batch Mode: Try to send all buffered packets
  // PassThrough Mode: NULL operation
  virtual Api::IoCallUint64Result flush() PURE;

  // Returns the name of the udp_packet_writer
  virtual std::string name() const PURE;

  // Returns the ioHandle associated with the udp_packet_writer
  virtual Network::IoHandle& getWriterIoHandle() const PURE;
};

using UdpPacketWriterPtr = std::unique_ptr<UdpPacketWriter>;

class UdpPacketWriterFactory {
public:
  virtual ~UdpPacketWriterFactory() = default;

  /**
   * Creates an UdpPacketWriter object for the given Udp Socket
   * @param socket UDP socket used to send packets.
   * @return the UdpPacketWriter created.
   */
  virtual UdpPacketWriterPtr createUdpPacketWriter(Network::Socket& socket) PURE;
};

using UdpPacketWriterFactoryPtr = std::unique_ptr<UdpPacketWriterFactory>;

} // namespace Network
} // namespace Envoy