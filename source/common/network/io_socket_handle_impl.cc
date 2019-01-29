#include "common/network/io_socket_handle_impl.h"

#include "common/common/assert.h"

#include <errno.h>
#include <iostream>

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
  return Api::OsSysCallsSingleton::get().readv(fd_, iovec, num_iovec);
}

SysCallSizeResult IoSocketHandleImpl::writev(const iovec* iovec, int num_iovec) {
  return Api::OsSysCallsSingleton::get().writev(fd_, iovec, num_iovec);
}

SysCallIntResult IoSocketHandleImpl::close() {
  ASSERT(fd_ != -1);
  const int rc = ::close(fd_);
  fd_ = -1;
  return {rc, errno};
}

bool IoSocketHandleImpl::isClosed() const { return fd_ == -1; }

SysCallIntResult IoSocketHandleImpl::bind(const sockaddr* addr, socklen_t addrlen) {
  return Api::OsSysCallsSingleton::get().bind(fd_, addr, addrlen);
}

SysCallIntResult IoSocketHandleImpl::connect(const struct sockaddr* serv_addr, socklen_t addrlen) {
  return Api::OsSysCallsSingleton::get().connect(fd_, serv_addr, addrlen);
}

SysCallIntResult IoSocketHandleImpl::setSocketOption(int level, int optname, const void* optval,
                                                     socklen_t optlen) {
  return Api::OsSysCallsSingleton::get().setsockopt(fd_, level, optname, optval, optlen);
}

SysCallIntResult IoSocketHandleImpl::getSocketOption(int level, int optname, void* optval,
                                                     socklen_t* optlen) const {
  return Api::OsSysCallsSingleton::get().getsockopt(fd_, level, optname, optval, optlen);
}

SysCallIntResult IoSocketHandleImpl::getSocketName(sockaddr* addr, socklen_t* addr_len) const {
  return Api::OsSysCallsSingleton::get().getsockname(fd_, addr, addr_len);
}

SysCallIntResult IoSocketHandleImpl::getPeerName(struct sockaddr* addr, socklen_t* addr_len) const {
  return Api::OsSysCallsSingleton::get().getpeername(fd_, addr, addr_len);
}

// static.
IoHandlePtr IoSocketHandleImpl::socket(int domain, int type, int protocol) {
  SysCallIntResult result = Api::OsSysCallsSingleton::get().socket(domain, type, protocol);
  RELEASE_ASSERT(result.rc_ != -1, "");
  return std::make_unique<IoSocketHandleImpl>(result.rc_);
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
