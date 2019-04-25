#include "common/api/os_sys_calls_impl.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

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

SysCallSizeResult OsSysCallsImpl::recvfrom(int sockfd, void* buffer, size_t length, int flags,
                                           struct sockaddr* addr, socklen_t* addrlen) {
  const ssize_t rc = ::recvfrom(sockfd, buffer, length, flags, addr, addrlen);
  return {rc, errno};
}

SysCallIntResult OsSysCallsImpl::shmOpen(const char* name, int oflag, mode_t mode) {
  const int rc = ::shm_open(name, oflag, mode);
  return {rc, errno};
}

SysCallIntResult OsSysCallsImpl::shmUnlink(const char* name) {
  const int rc = ::shm_unlink(name);
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

} // namespace Api
} // namespace Envoy
