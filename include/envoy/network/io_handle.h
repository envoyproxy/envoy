#pragma once

#include "envoy/api/io_error.h"
#include "envoy/common/platform.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Buffer {
struct RawSlice;
} // namespace Buffer

namespace Network {
namespace Address {
class Instance;
class Ip;
} // namespace Address

/**
 * IoHandle: an abstract interface for all I/O operations
 */
class IoHandle {
public:
  virtual ~IoHandle() = default;

  /**
   * Return data associated with IoHandle.
   *
   * TODO(danzh) move it to IoSocketHandle after replacing the calls to it with
   * calls to IoHandle API's everywhere.
   */
  virtual os_fd_t fd() const PURE;

  /**
   * Clean up IoHandle resources
   */
  virtual Api::IoCallUint64Result close() PURE;

  /**
   * Return true if close() hasn't been called.
   */
  virtual bool isOpen() const PURE;

  /**
   * Read data into given slices.
   * @param max_length supplies the maximum length to read.
   * @param slices points to the output location.
   * @param num_slice indicates the number of slices |slices| contains.
   * @return a Api::IoCallUint64Result with err_ = an Api::IoError instance or
   * err_ = nullptr and rc_ = the bytes read for success.
   */
  virtual Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                        uint64_t num_slice) PURE;

  /**
   * Write the data in slices out.
   * @param slices points to the location of data to be written.
   * @param num_slice indicates number of slices |slices| contains.
   * @return a Api::IoCallUint64Result with err_ = an Api::IoError instance or
   * err_ = nullptr and rc_ = the bytes written for success.
   */
  virtual Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slice) PURE;

  /**
   * Send a message to the address.
   * @param slices points to the location of data to be sent.
   * @param num_slice indicates number of slices |slices| contains.
   * @param self_ip is the source address whose port should be ignored. Nullptr
   * if caller wants kernel to select source address.
   * @param peer_address is the destination address.
   * @return a Api::IoCallUint64Result with err_ = an Api::IoError instance or
   * err_ = nullptr and rc_ = the bytes written for success.
   */
  virtual Api::IoCallUint64Result sendmsg(const Buffer::RawSlice* slices, uint64_t num_slice,
                                          int flags, const Address::Ip* self_ip,
                                          const Address::Instance& peer_address) PURE;

  struct RecvMsgOutput {
    /*
     * @param dropped_packets points to a variable to store how many packets are
     * dropped so far. If nullptr, recvmsg() won't try to get this information
     * from transport header.
     */
    RecvMsgOutput(uint32_t* dropped_packets) : dropped_packets_(dropped_packets) {}

    // If not nullptr, its value is the total number of packets dropped. recvmsg() will update it
    // when more packets are dropped.
    uint32_t* dropped_packets_;
    // The destination address from transport header.
    std::shared_ptr<const Address::Instance> local_address_;
    // The the source address from transport header.
    std::shared_ptr<const Address::Instance> peer_address_;
  };

  /**
   * Receive a message into given slices, output overflow, source/destination
   * addresses via passed-in parameters upon success.
   * @param slices points to the location of receiving buffer.
   * @param num_slice indicates number of slices |slices| contains.
   * @param self_port the port this handle is assigned to. This is used to populate
   * local_address because local port can't be retrieved from control message.
   * @param output modified upon each call to return fields requested in it.
   * @return a Api::IoCallUint64Result with err_ = an Api::IoError instance or
   * err_ = nullptr and rc_ = the bytes received for success.
   */
  virtual Api::IoCallUint64Result recvmsg(Buffer::RawSlice* slices, const uint64_t num_slice,
                                          uint32_t self_port, RecvMsgOutput& output) PURE;
};

using IoHandlePtr = std::unique_ptr<IoHandle>;

} // namespace Network
} // namespace Envoy
