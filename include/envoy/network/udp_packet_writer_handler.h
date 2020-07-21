#pragma once

#include <cstdint>
#include <memory>

#include "envoy/api/io_error.h"    // IoCallUint64Result
#include "envoy/buffer/buffer.h"   // Buffer
#include "envoy/network/address.h" // Address
#include "envoy/network/socket.h"  // Socket
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Network {

#define UDP_PACKET_WRITER_STATS(GAUGE)                                                             \
  GAUGE(internal_buffer_size, NeverImport)                                                         \
  GAUGE(last_buffered_msg_size, NeverImport)                                                       \
  GAUGE(sent_bytes, NeverImport)

/**
 * Wrapper struct for udp packet writer stats. @see stats_macros.h
 */
struct UdpPacketWriterStats {
  UDP_PACKET_WRITER_STATS(GENERATE_GAUGE_STRUCT)
};

class UdpPacketWriter {
public:
  virtual ~UdpPacketWriter() = default;

  /**
   * Sends a packet via given UDP socket with specific source address.
   * @param buffer points to the buffer containing the packet
   * @param local_ip is the source address to be used to send.
   * @param peer_address is the destination address to send to.
   */
  virtual Api::IoCallUint64Result writePacket(const Buffer::Instance& buffer,
                                              const Address::Ip* local_ip,
                                              const Address::Instance& peer_address) PURE;

  /**
   * @returns true if the network socket is not writable.
   */
  virtual bool isWriteBlocked() const PURE;

  /**
   * @brief records that the socket has become writable, for example when an EPOLLOUT
   * is received or an asynchronous write completes.
   */
  virtual void setWritable() PURE;

  /**
   * @brief get the maximum size of the packet which can be written using this
   * writer for the supplied peer address.
   *
   * @param peer_address  is the destination address to send to.
   * @return uint64_t the Max Packet Size object
   */
  virtual uint64_t getMaxPacketSize(const Address::Instance& peer_address) const PURE;

  /**
   * @return true if Batch Mode
   * @return false if PassThroughMode
   */
  virtual bool isBatchMode() const PURE;

  // TODO(yugant): Change char* below return to a struct {char*, fn_ptr}. Explanation below.
  //
  // Is it okay to skip the release_buffer function pointer, and only return char*?
  // For GsoBatchWriter Yes, since it will always return nullptr for the function ptr
  // But other BatchWriter implementations may be using release_buffer fn ptr to
  // release buffers from a free_list. Hence for that it would be better to have the return
  // type here as a struct with both char* and function ptr.

  /**
   * @brief get pointer to the next write location in internal buffer,
   * it should be called iff the caller does not call writePacket
   * for the returned buffer.
   *
   * @param local_ip is the source address to be used to send.
   * @param peer_address is the destination address to send to.
   * @return char* pointer to the next write location
   */
  virtual char* getNextWriteLocation(const Address::Ip* local_ip,
                                     const Address::Instance& peer_address) PURE;

  /**
   * @brief batch Mode: Try to send all buffered packets
   *        passThrough Mode: NULL operation
   *
   * @return Api::IoCallUint64Result
   */
  virtual Api::IoCallUint64Result flush() PURE;

  /**
   * @return std::string the name of the udp_packet_writer
   */
  virtual std::string name() const PURE;

  /**
   * @brief get the io handle associated with the udp_packet_writer
   */
  virtual Network::IoHandle& getWriterIoHandle() const PURE;

  /**
   * @brief get the Udp Packet Writer Stats object
   */
  virtual UdpPacketWriterStats getUdpPacketWriterStats() PURE;
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
  virtual UdpPacketWriterPtr createUdpPacketWriter(Network::IoHandle& io_handle,
                                                   Stats::Scope& scope) PURE;
};

using UdpPacketWriterFactoryPtr = std::unique_ptr<UdpPacketWriterFactory>;

} // namespace Network
} // namespace Envoy