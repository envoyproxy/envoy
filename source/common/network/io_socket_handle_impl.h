#pragma once

#include "envoy/api/io_error.h"
#include "envoy/api/os_sys_calls.h"
#include "envoy/network/io_handle.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Network {

/**
 * IoHandle derivative for sockets
 */
class IoSocketHandleImpl : public IoHandle, protected Logger::Loggable<Logger::Id::io> {
public:
  explicit IoSocketHandleImpl(int fd = -1) : fd_(fd) {}

  // Close underlying socket if close() hasn't been call yet.
  ~IoSocketHandleImpl() override;

  // TODO(sbelair2)  To be removed when the fd is fully abstracted from clients.
  int fd() const override { return fd_; }

  Api::IoCallUint64Result close() override;

  bool isOpen() const override;

  Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                uint64_t num_slice) override;

  Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slice) override;

  Api::IoCallUint64Result sendto(const Buffer::RawSlice& slice, int flags,
                                 const Address::Instance& address) override;

  Api::IoCallUint64Result sendmsg(const Buffer::RawSlice* slices, uint64_t num_slice, int flags,
                                  const Address::Instance& address) override;

  Api::IoCallUint64Result recvmsg(Buffer::RawSlice* slices, const uint64_t num_slice,
                                  uint32_t self_port, bool v6only, uint32_t* dropped_packets,
                                  std::shared_ptr<const Address::Instance>& local_address,
                                  std::shared_ptr<const Address::Instance>& peer_address) override;

private:
  // This is the structure that SO_TIMESTAMPING fills into the cmsg header. It is
  // well-defined, but does not have a definition in a public header. See
  // https://www.kernel.org/doc/Documentation/networking/timestamping.txt for more
  // information.
  struct LinuxTimestamping {
    // The converted system time of the timestamp.
    struct timespec systime;
    // Deprecated; serves only as padding.
    struct timespec hwtimetrans;
    // The raw hardware timestamp.
    struct timespec hwtimeraw;
  };

  // Converts a SysCallSizeResult to IoCallUint64Result.
  Api::IoCallUint64Result sysCallResultToIoCallResult(const Api::SysCallSizeResult& result);

  int fd_;
};

} // namespace Network
} // namespace Envoy
