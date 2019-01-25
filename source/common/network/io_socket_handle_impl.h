#pragma once

#include "envoy/network/io_handle.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Network {

/**
 * IoHandle derivative for sockets
 */
class IoSocketHandle : public IoHandle {
public:
  IoSocketHandle(int fd = -1) : fd_(fd) {}

  // TODO(sbelair2) Call close() in destructor
  ~IoSocketHandle() { ASSERT(fd_ == -1); }

  // TODO(sbelair2)  To be removed when the fd is fully abstracted from clients.
  int fd() const override { return fd_; }

  // Currently this close() is just for the IoHandle, and the close() system call
  // happens elsewhere. In coming changes, the close() syscall will be made from the IoHandle.
  // In particular, the close should also close the fd.
  void close() override { fd_ = -1; }

private:
  int fd_;
};
typedef std::unique_ptr<IoSocketHandle> IoSocketHandlePtr;

} // namespace Network
} // namespace Envoy
