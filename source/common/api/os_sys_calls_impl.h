#pragma once

#include "envoy/api/os_sys_calls.h"

#include "common/singleton/threadsafe_singleton.h"

namespace Envoy {
namespace Api {

class OsSysCallsImpl : public OsSysCalls {
public:
  // Api::OsSysCalls
  int bind(int sockfd, const sockaddr* addr, socklen_t addrlen) override;
  int open(const std::string& full_path, int flags, int mode) override;
  ssize_t write(int fd, const void* buffer, size_t num_bytes) override;
  ssize_t recv(int socket, void* buffer, size_t length, int flags) override;
  int close(int fd) override;
  int shmOpen(const char* name, int oflag, mode_t mode) override;
  int shmUnlink(const char* name) override;
  int ftruncate(int fd, off_t length) override;
  void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) override;
  int stat(const char* pathname, struct stat* buf) override;
  int setsockopt(int sockfd, int level, int optname, const void* optval, socklen_t optlen) override;
  int getsockopt(int sockfd, int level, int optname, void* optval, socklen_t* optlen) override;
};

typedef ThreadSafeSingleton<OsSysCallsImpl> OsSysCallsSingleton;

} // namespace Api
} // namespace Envoy
