#pragma once

#include "envoy/network/io_handle.h"

namespace Envoy {
namespace Network {

/**
 * IoHandle derivative for sockets
 */
class IoSocketHandle : public IoHandle {
public:
  IoSocketHandle(int fd = -1) : fd_(fd) {}

  // TODO(sbelair2) Call close() in destructor
  ~IoSocketHandle() {}

  // TODO(sbelair2)  To be removed when the fd is fully abstracted from clients.
  int fd() const override { return fd_; }

  void close() override { fd_ = -1; }

  // TODO(sbelair2)  To be removed when the fd is fully abstracted from clients.
  void operator=(int fd) { fd_ = fd; }

private:
  int fd_;
};
typedef std::unique_ptr<IoSocketHandle> IoSocketHandlePtr;

} // namespace Network
} // namespace Envoy
