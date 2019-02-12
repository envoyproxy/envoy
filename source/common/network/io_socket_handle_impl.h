#pragma once

#include "envoy/network/io_handle.h"
#include "envoy/api/os_sys_calls.h"

namespace Envoy {
namespace Network {

class IoSocketError : public IoError {
public:
  ~IoSocketError() override {}

  IoErrorCode getErrorCode() const override;

  std::string getErrorDetails() const override;
};


/**
 * IoHandle derivative for sockets
 */
class IoSocketHandleImpl : public IoHandle {
public:
  explicit IoSocketHandleImpl(int fd = -1) : fd_(fd) {}

  ~IoSocketHandleImpl() override;

  // TODO(sbelair2)  To be removed when the fd is fully abstracted from clients.
  int fd() const override { return fd_; }

  IoHandleCallIntResult close() override;

  bool isClosed() const override;

private:
  int fd_;
};

} // namespace Network
} // namespace Envoy
