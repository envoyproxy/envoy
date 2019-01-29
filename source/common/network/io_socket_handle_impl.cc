#include "common/network/io_socket_handle_impl.h"

#include <errno.h>

#include <iostream>

#include "common/common/assert.h"

using Envoy::Api::SysCallIntResult;
using Envoy::Api::SysCallSizeResult;

namespace Envoy {
namespace Network {

IoSocketHandleImpl::~IoSocketHandleImpl() {
  if (fd_ != -1) {
    close();
  }
}

SysCallSizeResult IoSocketHandleImpl::readv(const iovec* iovec, int num_iovec) {
  const ssize_t rc = ::readv(fd_, iovec, num_iovec);
  return {rc, errno};
}

SysCallSizeResult IoSocketHandleImpl::writev(const iovec* iovec, int num_iovec) {
  const ssize_t rc = ::writev(fd_, iovec, num_iovec);
  return {rc, errno};
}

SysCallIntResult IoSocketHandleImpl::close() {
  ASSERT(fd_ != -1);
  const int rc = ::close(fd_);
  fd_ = -1;
  return {rc, errno};
}

bool IoSocketHandleImpl::isClosed() const { return fd_ == -1; }

SysCallIntResult IoSocketHandleImpl::bind(const sockaddr* addr, socklen_t addrlen) {
  const int rc = ::bind(fd_, addr, addrlen);
  return {rc, errno};
}

SysCallIntResult IoSocketHandleImpl::connect(const struct sockaddr* serv_addr, socklen_t addrlen) {
  const int rc = ::connect(fd_, serv_addr, addrlen);
  return {rc, errno};
}

SysCallIntResult IoSocketHandleImpl::setSocketOption(int level, int optname, const void* optval,
                                                     socklen_t optlen) {
  const int rc = ::setsockopt(fd_, level, optname, optval, optlen);
  return {rc, errno};
}

SysCallIntResult IoSocketHandleImpl::getSocketOption(int level, int optname, void* optval,
                                                     socklen_t* optlen) const {
  const int rc = ::getsockopt(fd_, level, optname, optval, optlen);
  return {rc, errno};
}

SysCallIntResult IoSocketHandleImpl::getSocketName(sockaddr* addr, socklen_t* addr_len) const {
  const int rc = ::getsockname(fd_, addr, addr_len);
  return {rc, errno};
}

SysCallIntResult IoSocketHandleImpl::getPeerName(struct sockaddr* addr, socklen_t* addr_len) const {
  const int rc = ::getpeername(fd_, addr, addr_len);
  return {rc, errno};
}

// static.
IoHandlePtr IoSocketHandleImpl::socket(int domain, int type, int protocol) {
  const int rc = ::socket(domain, type, protocol);
  RELEASE_ASSERT(rc != -1, "");
  return std::make_unique<IoSocketHandleImpl>(rc);
}

SysCallIntResult IoSocketHandleImpl::setSocketFlag(int flag) {
  const int rc = ::fcntl(fd_, F_SETFL, flag);
  return {rc, errno};
}

SysCallIntResult IoSocketHandleImpl::getSocketFlag() const {
  const int rc = ::fcntl(fd_, F_GETFL, 0);
  return {rc, errno};
}

Api::SysCallIntResult IoSocketHandleImpl::listen(int backlog) {
  const int rc = ::listen(fd_, backlog);
  return {rc, errno};
}

IoHandlePtr IoSocketHandleImpl::dup() const {
  const int rc = ::dup(fd_);
  if (rc == -1) {
    return nullptr;
  }
  return IoHandlePtr(new IoSocketHandleImpl(rc));
}

Api::SysCallIntResult IoSocketHandleImpl::shutdown(int how) {
  const int rc = ::shutdown(fd_, how);
  return {rc, errno};
}

} // namespace Network
} // namespace Envoy
