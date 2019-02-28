#include "common/api/os_sys_calls_impl_linux.h"

#include <errno.h>
#include <sched.h>

namespace Envoy {
namespace Api {

SysCallIntResult LinuxOsSysCallsImpl::bind(int sockfd, const sockaddr* addr, socklen_t addrlen) {
  return os_sys_calls_.bind(sockfd, addr, addrlen);
}

SysCallIntResult LinuxOsSysCallsImpl::ioctl(int sockfd, unsigned long int request, void* argp) {
  return os_sys_calls_.ioctl(sockfd, request, argp);
}

SysCallIntResult LinuxOsSysCallsImpl::close(int fd) { return os_sys_calls_.close(fd); }

SysCallSizeResult LinuxOsSysCallsImpl::writev(int fd, const iovec* iovec, int num_iovec) {
  return os_sys_calls_.writev(fd, iovec, num_iovec);
}

SysCallSizeResult LinuxOsSysCallsImpl::readv(int fd, const iovec* iovec, int num_iovec) {
  return os_sys_calls_.readv(fd, iovec, num_iovec);
}

SysCallSizeResult LinuxOsSysCallsImpl::recv(int socket, void* buffer, size_t length, int flags) {
  return os_sys_calls_.recv(socket, buffer, length, flags);
}

SysCallIntResult LinuxOsSysCallsImpl::shmOpen(const char* name, int oflag, mode_t mode) {
  return os_sys_calls_.shmOpen(name, oflag, mode);
}

SysCallIntResult LinuxOsSysCallsImpl::shmUnlink(const char* name) {
  return os_sys_calls_.shmUnlink(name);
}

SysCallIntResult LinuxOsSysCallsImpl::ftruncate(int fd, off_t length) {
  return os_sys_calls_.ftruncate(fd, length);
}

SysCallPtrResult LinuxOsSysCallsImpl::mmap(void* addr, size_t length, int prot, int flags, int fd,
                                           off_t offset) {
  return os_sys_calls_.mmap(addr, length, prot, flags, fd, offset);
}

SysCallIntResult LinuxOsSysCallsImpl::stat(const char* pathname, struct stat* buf) {
  return os_sys_calls_.stat(pathname, buf);
}

SysCallIntResult LinuxOsSysCallsImpl::setsockopt(int sockfd, int level, int optname,
                                                 const void* optval, socklen_t optlen) {
  return os_sys_calls_.setsockopt(sockfd, level, optname, optval, optlen);
}

SysCallIntResult LinuxOsSysCallsImpl::getsockopt(int sockfd, int level, int optname, void* optval,
                                                 socklen_t* optlen) {
  return os_sys_calls_.getsockopt(sockfd, level, optname, optval, optlen);
}

SysCallIntResult LinuxOsSysCallsImpl::socket(int domain, int type, int protocol) {
  return os_sys_calls_.socket(domain, type, protocol);
}

SysCallIntResult LinuxOsSysCallsImpl::sched_getaffinity(pid_t pid, size_t cpusetsize,
                                                        cpu_set_t* mask) {
  const int rc = ::sched_getaffinity(pid, cpusetsize, mask);
  return {rc, errno};
}

} // namespace Api
} // namespace Envoy
