#pragma once

#include "envoy/api/io_error.h"
#include "envoy/api/os_sys_calls.h"
#include "envoy/common/platform.h"
#include "envoy/network/io_handle.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Network {

/**
 * IoHandle derivative for sockets
 */
class IoSocketHandleImpl : public IoHandle, protected Logger::Loggable<Logger::Id::io> {
public:
  explicit IoSocketHandleImpl(os_fd_t fd = INVALID_SOCKET) : fd_(fd) {}

  // Close underlying socket if close() hasn't been call yet.
  ~IoSocketHandleImpl() override;

  // TODO(sbelair2)  To be removed when the fd is fully abstracted from clients.
  os_fd_t fd() const override { return fd_; }

  Api::IoCallUint64Result close() override;

  bool isOpen() const override;

  Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                uint64_t num_slice) override;

  Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slice) override;

  Api::IoCallUint64Result sendmsg(const Buffer::RawSlice* slices, uint64_t num_slice, int flags,
                                  const Address::Ip* self_ip,
                                  const Address::Instance& peer_address) override;

  Api::IoCallUint64Result recvmsg(Buffer::RawSlice* slices, const uint64_t num_slice,
                                  uint32_t self_port, RecvMsgOutput& output) override;

private:
  // Converts a SysCallSizeResult to IoCallUint64Result.
  Api::IoCallUint64Result sysCallResultToIoCallResult(const Api::SysCallSizeResult& result);

  os_fd_t fd_;
};

} // namespace Network
} // namespace Envoy
