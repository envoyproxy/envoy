#pragma once

#include "envoy/api/os_sys_calls.h"

#include "common/singleton/threadsafe_singleton.h"

namespace Envoy {
namespace Api {

class OsSysCallsImpl : public OsSysCalls {
public:
  // Api::OsSysCalls
  SysCallIntResult bind(int sockfd, const sockaddr* addr, socklen_t addrlen) override;
  SysCallIntResult ioctl(int sockfd, unsigned long int request, void* argp) override;
  SysCallIntResult open(const std::string& full_path, int flags, int mode) override;
  SysCallSizeResult write(int fd, const void* buffer, size_t num_bytes) override;
  SysCallSizeResult writev(int fd, const iovec* iovec, int num_iovec) override;
  SysCallSizeResult readv(int fd, const iovec* iovec, int num_iovec) override;
  SysCallSizeResult recv(int socket, void* buffer, size_t length, int flags) override;
  SysCallIntResult close(int fd) override;
  SysCallIntResult shmOpen(const char* name, int oflag, mode_t mode) override;
  SysCallIntResult shmUnlink(const char* name) override;
  SysCallIntResult ftruncate(int fd, off_t length) override;
  SysCallPtrResult mmap(void* addr, size_t length, int prot, int flags, int fd,
                        off_t offset) override;
  SysCallIntResult stat(const char* pathname, struct stat* buf) override;
  SysCallIntResult setsockopt(int sockfd, int level, int optname, const void* optval,
                              socklen_t optlen) override;
  SysCallIntResult getsockopt(int sockfd, int level, int optname, void* optval,
                              socklen_t* optlen) override;
  SysCallIntResult socket(int domain, int type, int protocol) override;
};

typedef ThreadSafeSingleton<OsSysCallsImpl> OsSysCallsSingleton;

} // namespace Api
} // namespace Envoy
