#pragma once

#include <memory>

#include "envoy/api/io_error.h"
#include "envoy/common/platform.h"
#include "envoy/common/pure.h"
#include "envoy/network/address.h"

#include "absl/container/fixed_array.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Buffer {
struct RawSlice;
} // namespace Buffer

using RawSliceArrays = absl::FixedArray<absl::FixedArray<Buffer::RawSlice>>;

namespace Network {

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

  struct RecvMsgPerPacketInfo {
    // The destination address from transport header.
    Address::InstanceConstSharedPtr local_address_;
    // The the source address from transport header.
    Address::InstanceConstSharedPtr peer_address_;
    // The payload length of this packet.
    unsigned int msg_len_{0};
    // The gso_size, if specified in the transport header
    unsigned int gso_size_{0};
  };

  /**
   * The output parameter type for recvmsg and recvmmsg.
   */
  struct RecvMsgOutput {
    /*
     * @param num_packets_per_call is the max number of packets allowed per
     * recvmmsg call. For recvmsg call, any value larger than 0 is allowed, but
     * only one packet will be returned.
     * @param dropped_packets points to a variable to store how many packets are
     * dropped so far. If nullptr, recvmsg() won't try to get this information
     * from transport header.
     */
    RecvMsgOutput(size_t num_packets_per_call, uint32_t* dropped_packets)
        : dropped_packets_(dropped_packets), msg_(num_packets_per_call) {}

    // If not nullptr, its value is the total number of packets dropped. recvmsg() will update it
    // when more packets are dropped.
    uint32_t* dropped_packets_;

    // Packet headers for each received packet. It's populated according to packet receive order.
    // Only the first entry is used to return per packet information by recvmsg.
    absl::FixedArray<RecvMsgPerPacketInfo> msg_;
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

  /**
   * If the platform supports, receive multiple messages into given slices, output overflow,
   * source/destination addresses per message via passed-in parameters upon success.
   * @param slices are the receive buffers for the messages. Each message
   * received are stored in an individual entry of |slices|.
   * @param self_port is the same as the one in recvmsg().
   * @param output is modified upon each call and each message received.
   */
  virtual Api::IoCallUint64Result recvmmsg(RawSliceArrays& slices, uint32_t self_port,
                                           RecvMsgOutput& output) PURE;

  /**
   * return true if the platform supports recvmmsg() and sendmmsg().
   */
  virtual bool supportsMmsg() const PURE;

  /**
   * return true if the platform supports udp_gro
   */
  virtual bool supportsUdpGro() const PURE;

  /**
   * Bind to address. The handle should have been created with a call to socket()
   * @param address address to bind to.
   * @param addrlen address length
   * @return a Api::SysCallIntResult with rc_ = 0 for success and rc_ = -1 for failure. If the call
   *   is successful, errno_ shouldn't be used.
   */
  virtual Api::SysCallIntResult bind(Address::InstanceConstSharedPtr address) PURE;

  /**
   * Listen on bound handle.
   * @param backlog maximum number of pending connections for listener
   * @return a Api::SysCallIntResult with rc_ = 0 for success and rc_ = -1 for failure. If the call
   *   is successful, errno_ shouldn't be used.
   */
  virtual Api::SysCallIntResult listen(int backlog) PURE;

  /**
   * Connect to address. The handle should have been created with a call to socket()
   * on this object.
   * @param address remote address to connect to.
   * @param addrlen remote address length
   * @return a Api::SysCallIntResult with rc_ = 0 for success and rc_ = -1 for failure. If the call
   *   is successful, errno_ shouldn't be used.
   */
  virtual Api::SysCallIntResult connect(Address::InstanceConstSharedPtr address) PURE;

  /**
   * Set option (see man 2 setsockopt)
   */
  virtual Api::SysCallIntResult setOption(int level, int optname, const void* optval,
                                          socklen_t optlen) PURE;

  /**
   * Get option (see man 2 getsockopt)
   */
  virtual Api::SysCallIntResult getOption(int level, int optname, void* optval,
                                          socklen_t* optlen) PURE;

  /**
   * Toggle blocking behavior
   * @param blocking flag to set/unset blocking state
   * @return a Api::SysCallIntResult with rc_ = 0 for success and rc_ = -1 for failure. If the call
   * is successful, errno_ shouldn't be used.
   */
  virtual Api::SysCallIntResult setBlocking(bool blocking) PURE;

  /**
   * Get domain used by underlying socket (see man 2 socket)
   * @param domain updated to the underlying socket's domain if call is successful
   * @return a Api::SysCallIntResult with rc_ = 0 for success and rc_ = -1 for failure. If the call
   * is successful, errno_ shouldn't be used.
   */
  virtual absl::optional<int> domain() PURE;

  /**
   * Get local address (ip:port pair)
   * @return local address as @ref Address::InstanceConstSharedPtr
   */
  virtual Address::InstanceConstSharedPtr localAddress() PURE;

  /**
   * Get peer's address (ip:port pair)
   * @return peer's address as @ref Address::InstanceConstSharedPtr
   */
  virtual Address::InstanceConstSharedPtr peerAddress() PURE;
};

using IoHandlePtr = std::unique_ptr<IoHandle>;

} // namespace Network
} // namespace Envoy
