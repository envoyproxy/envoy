#pragma once

#include "envoy/network/io_handle.h"

namespace Envoy {
namespace Network {

/**
 * IoHandle derivative for sockets
 */
class IoSocketHandleImpl : public IoHandle {
public:
  explicit IoSocketHandleImpl(int fd = -1) : fd_(fd) {}

  ~IoSocketHandleImpl() override;

  // TODO(sbelair2)  To be removed when the fd is fully abstracted from clients.
  int fd() const override { return fd_; }

  Api::SysCallIntResult close() override;

  Api::SysCallSizeResult readv(const iovec* iovec, int num_iovec) override;

  Api::SysCallSizeResult writev(const iovec* iovec, int num_iovec) override;

  bool isClosed() const override;

  Api::SysCallIntResult bind(const sockaddr* addr, socklen_t addrlen) override;

  Api::SysCallIntResult connect(const struct sockaddr* serv_addr, socklen_t addrlen) override;

  Api::SysCallIntResult setSocketOption(int level, int optname, const void* optval,
                                        socklen_t optlen) override;

  Api::SysCallIntResult getSocketOption(int level, int optname, void* optval,
                                        socklen_t* optlen) const override;

  Api::SysCallIntResult getSocketName(struct sockaddr* addr, socklen_t* addr_len) const override;

  Api::SysCallIntResult getPeerName(struct sockaddr* addr, socklen_t* addr_len) const override;

  static IoHandlePtr socket(int domain, int type, int protocol);

  Api::SysCallIntResult setSocketFlag(int flag) override;

  Api::SysCallIntResult getSocketFlag() const override;

  Api::SysCallIntResult listen(int backlog) override;

  Api::SysCallIntResult shutdown(int how) override;

  IoHandlePtr dup() const override;

private:
  int fd_;
};

} // namespace Network
} // namespace Envoy
