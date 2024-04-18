#pragma once

#include <cstdint>
#include <memory>

#include "envoy/api/io_error.h"
#include "envoy/buffer/buffer.h"
#include "envoy/common/optref.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/network/address.h"
#include "envoy/network/socket.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Network {

/**
 * Max v6 packet size, excluding IP and UDP headers.
 */
constexpr uint64_t UdpMaxOutgoingPacketSize = 1452;

/**
 * UdpPacketWriterBuffer bundles a buffer and a function that
 * releases it.
 */
struct UdpPacketWriterBuffer {
  UdpPacketWriterBuffer() = default;
  UdpPacketWriterBuffer(uint8_t* buffer, size_t length,
                        std::function<void(const char*)> release_buffer)
      : buffer_(buffer), length_(length), release_buffer_(std::move(release_buffer)) {}

  uint8_t* buffer_ = nullptr;
  size_t length_ = 0;
  std::function<void(const char*)> release_buffer_;
};

class UdpPacketWriter {
public:
  virtual ~UdpPacketWriter() = default;

  /**
   * @brief Sends a packet via given UDP socket with specific source address.
   *
   * @param buffer points to the buffer containing the packet
   * @param local_ip is the source address to be used to send. If it is null,
   * picks up the default network interface ip address.
   * @param peer_address is the destination address to send to.
   * @return result with number of bytes written, and write status
   */
  virtual Api::IoCallUint64Result writePacket(const Buffer::Instance& buffer,
                                              const Address::Ip* local_ip,
                                              const Address::Instance& peer_address) PURE;

  /**
   * @returns true if the network socket is not writable.
   */
  virtual bool isWriteBlocked() const PURE;

  /**
   * @brief mark the socket as writable when the socket is unblocked.
   */
  virtual void setWritable() PURE;

  /**
   * @brief Get the maximum size of the packet which can be written using this
   * writer for the supplied peer address.
   *
   * @param peer_address  is the destination address to send to.
   * @return the max packet size
   */
  virtual uint64_t getMaxPacketSize(const Address::Instance& peer_address) const PURE;

  /**
   * @return true if Batch Mode
   * @return false if PassThroughMode
   */
  virtual bool isBatchMode() const PURE;

  /**
   * @brief Get pointer to the next write location in internal buffer,
   * it should be called iff the caller does not call writePacket
   * for the returned buffer. The caller is expected to call writePacket
   * with the buffer returned from this function to save a memcpy.
   *
   * @param local_ip is the source address to be used to send.
   * @param peer_address is the destination address to send to.
   * @return { char* to the next write location,
   *           func to release buffer }
   */
  virtual UdpPacketWriterBuffer getNextWriteLocation(const Address::Ip* local_ip,
                                                     const Address::Instance& peer_address) PURE;

  /**
   * @brief Batch Mode: Try to send all buffered packets
   *        PassThrough Mode: NULL operation
   *
   * @return Api::IoCallUint64Result
   */
  virtual Api::IoCallUint64Result flush() PURE;
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

/**
 * UdpPacketWriterFactoryFactory adds an extra layer of indirection In order to
 * support a UdpPacketWriterFactory whose behavior depends on the
 * TypedConfig for that factory. The UdpPacketWriterFactoryFactory is created
 * with a no-arg constructor based on the type of the config. Then this
 * `createUdpPacketWriterFactory1 can be called with the config to
 * create an actual UdpPacketWriterFactory.
 */
class UdpPacketWriterFactoryFactory : public Envoy::Config::TypedFactory {
public:
  ~UdpPacketWriterFactoryFactory() override = default;

  /**
   * Creates an UdpPacketWriterFactory based on the specified config.
   * @return the UdpPacketWriterFactory created.
   */
  virtual UdpPacketWriterFactoryPtr
  createUdpPacketWriterFactory(const envoy::config::core::v3::TypedExtensionConfig& config) PURE;

  std::string category() const override { return "envoy.udp_packet_writer"; }
};

} // namespace Network
} // namespace Envoy
