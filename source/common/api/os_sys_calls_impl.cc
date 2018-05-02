#include "common/api/os_sys_calls_impl.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Envoy {
namespace Api {

int OsSysCallsImpl::bind(int sockfd, const sockaddr* addr, socklen_t addrlen) {
  return ::bind(sockfd, addr, addrlen);
}

int OsSysCallsImpl::open(const std::string& full_path, int flags, int mode) {
  return ::open(full_path.c_str(), flags, mode);
}

int OsSysCallsImpl::close(int fd) { return ::close(fd); }

ssize_t OsSysCallsImpl::write(int fd, const void* buffer, size_t num_bytes) {
  return ::write(fd, buffer, num_bytes);
}

ssize_t OsSysCallsImpl::recv(int socket, void* buffer, size_t length, int flags) {
  return ::recv(socket, buffer, length, flags);
}

int OsSysCallsImpl::shmOpen(const char* name, int oflag, mode_t mode) {
  return ::shm_open(name, oflag, mode);
}

int OsSysCallsImpl::shmUnlink(const char* name) { return ::shm_unlink(name); }

int OsSysCallsImpl::ftruncate(int fd, off_t length) { return ::ftruncate(fd, length); }

void* OsSysCallsImpl::mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
  return ::mmap(addr, length, prot, flags, fd, offset);
}

int OsSysCallsImpl::stat(const char* pathname, struct stat* buf) { return ::stat(pathname, buf); }

int OsSysCallsImpl::setsockopt(int sockfd, int level, int optname, const void* optval,
                               socklen_t optlen) {
  return ::setsockopt(sockfd, level, optname, optval, optlen);
}

int OsSysCallsImpl::getsockopt(int sockfd, int level, int optname, void* optval,
                               socklen_t* optlen) {
  return ::getsockopt(sockfd, level, optname, optval, optlen);
}

} // namespace Api
} // namespace Envoy
