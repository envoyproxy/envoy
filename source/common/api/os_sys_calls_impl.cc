#include "common/api/os_sys_calls_impl.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>

namespace Envoy {
namespace Api {

SysCallIntResult OsSysCallsImpl::bind(int sockfd, const sockaddr* addr, socklen_t addrlen) {
  const int rc = ::bind(sockfd, addr, addrlen);
  return {rc, errno};
}

SysCallIntResult OsSysCallsImpl::ioctl(int sockfd, unsigned long int request, void* argp) {
  const int rc = ::ioctl(sockfd, request, argp);
  return {rc, errno};
}

SysCallIntResult OsSysCallsImpl::close(int fd) {
  const int rc = ::close(fd);
  return {rc, errno};
}

SysCallSizeResult OsSysCallsImpl::writev(int fd, const iovec* iovec, int num_iovec) {
  const ssize_t rc = ::writev(fd, iovec, num_iovec);
  return {rc, errno};
}

SysCallSizeResult OsSysCallsImpl::readv(int fd, const iovec* iovec, int num_iovec) {
  const ssize_t rc = ::readv(fd, iovec, num_iovec);
  return {rc, errno};
}

SysCallSizeResult OsSysCallsImpl::recv(int socket, void* buffer, size_t length, int flags) {
  const ssize_t rc = ::recv(socket, buffer, length, flags);
  return {rc, errno};
}

SysCallSizeResult OsSysCallsImpl::recvmsg(int sockfd, struct msghdr* msg, int flags) {
  const ssize_t rc = ::recvmsg(sockfd, msg, flags);
  return {rc, errno};
}

SysCallIntResult OsSysCallsImpl::ftruncate(int fd, off_t length) {
  const int rc = ::ftruncate(fd, length);
  return {rc, errno};
}

SysCallPtrResult OsSysCallsImpl::mmap(void* addr, size_t length, int prot, int flags, int fd,
                                      off_t offset) {
  void* rc = ::mmap(addr, length, prot, flags, fd, offset);
  return {rc, errno};
}

SysCallIntResult OsSysCallsImpl::stat(const char* pathname, struct stat* buf) {
  const int rc = ::stat(pathname, buf);
  return {rc, errno};
}

SysCallIntResult OsSysCallsImpl::setsockopt(int sockfd, int level, int optname, const void* optval,
                                            socklen_t optlen) {
  const int rc = ::setsockopt(sockfd, level, optname, optval, optlen);
  return {rc, errno};
}

SysCallIntResult OsSysCallsImpl::getsockopt(int sockfd, int level, int optname, void* optval,
                                            socklen_t* optlen) {
  const int rc = ::getsockopt(sockfd, level, optname, optval, optlen);
  return {rc, errno};
}

SysCallIntResult OsSysCallsImpl::socket(int domain, int type, int protocol) {
  const int rc = ::socket(domain, type, protocol);
  return {rc, errno};
}

SysCallSizeResult OsSysCallsImpl::sendmsg(int fd, const msghdr* message, int flags) {
  const int rc = ::sendmsg(fd, message, flags);
  return {rc, errno};
}

SysCallIntResult OsSysCallsImpl::getsockname(int sockfd, sockaddr* addr, socklen_t* addrlen) {
  const int rc = ::getsockname(sockfd, addr, addrlen);
  return {rc, errno};
}

} // namespace Api
} // namespace Envoy
