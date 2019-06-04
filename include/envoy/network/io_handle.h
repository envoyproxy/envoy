#pragma once

#include "envoy/api/io_error.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Buffer {
struct RawSlice;
} // namespace Buffer

namespace Network {
namespace Address {
class Instance;
} // namespace Address

/**
 * IoHandle: an abstract interface for all I/O operations
 */
class IoHandle {
public:
  virtual ~IoHandle() {}

  /**
   * Return data associated with IoHandle.
   *
   * TODO(danzh) move it to IoSocketHandle after replacing the calls to it with
   * calls to IoHandle API's everywhere.
   */
  virtual int fd() const PURE;

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
   * Send buffer to address.
   * @param slice points to the location of the data to be sent.
   * @param to_address is the destination address.
   * @return a Api::IoCallUint64Result with err_ = an Api::IoError instance or
   * err_ = nullptr and rc_ = the bytes written for success.
   */
  virtual Api::IoCallUint64Result sendto(const Buffer::RawSlice& slice, int flags,
                                         const Address::Instance& address) PURE;
  /**
   * Send a message to the address.
   * @param slices points to the location of data to be sent.
   * @param num_slice indicates number of slices |slices| contains.
   * @param address is the destination address.
   * @return a Api::IoCallUint64Result with err_ = an Api::IoError instance or
   * err_ = nullptr and rc_ = the bytes written for success.
   */
  virtual Api::IoCallUint64Result sendmsg(const Buffer::RawSlice* slices, uint64_t num_slice,
                                          int flags, const Address::Instance& address) PURE;

  /**
   * Receive a message into given slices, output overflow, source/destination
   * addresses via passed-in parameters upon success.
   * @param slices points to the location of receiving buffer.
   * @param num_slice indicates number of slices |slices| contains.
   * @param self_port the port this handle is assigned to. This is used to populate
   * local_address because local port can't be retrieved from control message.
   * @param v6only if true, disable IPv4-IPv6 mapping for IPv6 addresses.
   * @param dropped_packets number of packets dropped by kernel because of
   * receiving buffer overflow. Modified if not nullptr and there is overflow between
   * this call and previous one.
   * @param local_address the destination address from transport header.
   * Modified upon each call.
   * @param peer_address the source address from transport header. Modified upon
   * each call.
   * @return a Api::IoCallUint64Result with err_ = an Api::IoError instance or
   * err_ = nullptr and rc_ = the bytes received for success.
   */
  virtual Api::IoCallUint64Result
  recvmsg(Buffer::RawSlice* slices, const uint64_t num_slice, uint32_t self_port, bool v6only,
          uint32_t* dropped_packets, std::shared_ptr<const Address::Instance>& local_address,
          std::shared_ptr<const Address::Instance>& peer_address) PURE;
};

typedef std::unique_ptr<IoHandle> IoHandlePtr;

} // namespace Network
} // namespace Envoy
