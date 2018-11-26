#pragma once

#include "envoy/network/io_handle.h"

namespace Envoy {
namespace Network {

/**
 * IoHandle derivative for sockets
 */
class IoSocketHandle : public IoHandle {
public:
  /**
   * Construct from an existing Socket fd or without with default of -1.
   */
  IoSocketHandle(int fd = -1) : fd_(fd) {}

  /**
   * Copy Constructor from an existing IoSocketHandle.
   */
  IoSocketHandle(const IoSocketHandle& io_handle) : fd_(io_handle.fd()) {}

  ~IoSocketHandle() {}

  /**
   * TODO(sbelair2)  To be removed when the IoSocketHandle derivative is integrated
   * and the fd is fully abstracted from clients.
   *
   * @return the socket's file descriptor in the handle.
   */
  int fd() const override { return fd_; }

  /**
   * @param the socket file descriptor to set in the handle. Assigns an fd
   * from an external socket operation such as from libevent or the dispatcher after construction
   *
   * TODO(sbelair2)  To be removed when the IoSocketHandle derivative is integrated
   * and the fd is fully abstracted from clients.
   */
  void operator=(int fd) override { fd_ = fd; }

private:
  int fd_;
};
typedef std::unique_ptr<IoSocketHandle>* IoSocketHandlePtr;
typedef std::unique_ptr<const IoSocketHandle>* IoSocketHandleConstPtr;

} // namespace Network
} // namespace Envoy
