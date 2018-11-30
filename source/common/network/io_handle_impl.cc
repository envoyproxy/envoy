#include "common/network/io_handle_impl.h"

#include "common/common/assert.h"

#include <iostream>

using Envoy::Api::SysCallSizeResult;
using Envoy::Api::SysCallIntResult;

namespace Envoy {
namespace Network {

FdIoHandleImpl::FdIoHandleImpl(int fd) : fd_(fd) {}

SysCallSizeResult FdIoHandleImpl::readv(const iovec* iovec, int num_iovec) {
  return Api::OsSysCallsSingleton::get().readv(fd_, iovec, num_iovec);
}

SysCallSizeResult FdIoHandleImpl::writev(const iovec* iovec, int num_iovec) {
  return Api::OsSysCallsSingleton::get().writev(fd_, iovec, num_iovec);
}

SysCallIntResult FdIoHandleImpl::close() {
  auto result = Api::OsSysCallsSingleton::get().close(fd_);
  fd_ = -1;
  return result;
}

bool FdIoHandleImpl::isClosed() {
  return fd_ == -1;
}

SysCallIntResult FdIoHandleImpl::bind(const sockaddr* addr, socklen_t addrlen) {
  return Api::OsSysCallsSingleton::get().bind(fd_, addr, addrlen);
}

SysCallIntResult FdIoHandleImpl::connect(const struct sockaddr* serv_addr, socklen_t addrlen) {
  return Api::OsSysCallsSingleton::get().connect(fd_, serv_addr, addrlen);
}

SysCallIntResult FdIoHandleImpl::setSocketOption(int level, int optname, const void* optval,
                                                 socklen_t optlen) {
  std::cerr << "=========== setSocketOption os_sys_call " << &Api::OsSysCallsSingleton::get() << "\n";
  return Api::OsSysCallsSingleton::get().setsockopt(fd_, level, optname, optval, optlen);
}

SysCallIntResult FdIoHandleImpl::getSocketOption(int level, int optname, void* optval,
                                                 socklen_t* optlen) {
  return Api::OsSysCallsSingleton::get().getsockopt(fd_, level, optname, optval, optlen);
}

SysCallIntResult FdIoHandleImpl::getSocketName(sockaddr* addr, socklen_t* addr_len) {
    std::cerr << "=========== getSocketName os_sys_call " << &Api::OsSysCallsSingleton::get() << "\n";

  return Api::OsSysCallsSingleton::get().getsockname(fd_, addr, addr_len);
}

SysCallIntResult FdIoHandleImpl::getPeerName(struct sockaddr* addr, socklen_t* addr_len) {
  return Api::OsSysCallsSingleton::get().getpeername(fd_, addr, addr_len);
}

// static.
IoHandlePtr FdIoHandleImpl::socket(int domain, int type, int protocol) {
  SysCallIntResult result = Api::OsSysCallsSingleton::get().socket(domain, type, protocol);
  RELEASE_ASSERT(result.rc_ != -1, "");
  return IoHandlePtr(new FdIoHandleImpl(result.rc_));
}

SysCallIntResult FdIoHandleImpl::setSocketFlag(int flag) {
  const int rc = ::fcntl(fd_, F_SETFL, flag);
  return {rc, errno};
}

SysCallIntResult FdIoHandleImpl::getSocketFlag() {
  const int rc = ::fcntl(fd_, F_GETFL, 0);
  return {rc, errno};
}

Api::SysCallIntResult FdIoHandleImpl::listen(int backlog) {
  const int rc = ::listen(fd_, backlog);
  return {rc, errno};
}

IoHandlePtr FdIoHandleImpl::dup() {
  const int rc = ::dup(fd_);
  if (rc == -1) {
    return nullptr;
  }
  return IoHandlePtr(new FdIoHandleImpl(rc));
}

Api::SysCallIntResult FdIoHandleImpl::shutdown(int how) {
  const int rc = ::shutdown(fd_, how);
  return {rc, errno};
}

int FdIoHandleImpl::id() {
  return fd_;
}

} // namespace Network
} // namespace Envoy
