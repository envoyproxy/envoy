#pragma once

#include <string>

#include "envoy/api/os_sys_calls.h"

#include "source/common/singleton/threadsafe_singleton.h"

namespace Envoy {
namespace Api {

class OsSysCallsImpl : public OsSysCalls {
public:
  // Api::OsSysCalls
  SysCallIntResult bind(os_fd_t sockfd, const sockaddr* addr, socklen_t addrlen) override;
  SysCallIntResult chmod(const std::string& path, mode_t mode) override;
  SysCallIntResult ioctl(os_fd_t sockfd, unsigned long int request, void* argp, unsigned long,
                         void*, unsigned long, unsigned long*) override;
  SysCallSizeResult writev(os_fd_t fd, const iovec* iov, int num_iov) override;
  SysCallSizeResult readv(os_fd_t fd, const iovec* iov, int num_iov) override;
  SysCallSizeResult pwrite(os_fd_t fd, const void* buffer, size_t length,
                           off_t offset) const override;
  SysCallSizeResult pread(os_fd_t fd, void* buffer, size_t length, off_t offset) const override;
  SysCallSizeResult recv(os_fd_t socket, void* buffer, size_t length, int flags) override;
  SysCallSizeResult recvmsg(os_fd_t sockfd, msghdr* msg, int flags) override;
  SysCallIntResult recvmmsg(os_fd_t sockfd, struct mmsghdr* msgvec, unsigned int vlen, int flags,
                            struct timespec* timeout) override;
  bool supportsMmsg() const override;
  bool supportsUdpGro() const override;
  bool supportsUdpGso() const override;
  bool supportsIpTransparent() const override;
  bool supportsMptcp() const override;
  SysCallIntResult close(os_fd_t fd) override;
  SysCallIntResult ftruncate(int fd, off_t length) override;
  SysCallPtrResult mmap(void* addr, size_t length, int prot, int flags, int fd,
                        off_t offset) override;
  SysCallIntResult stat(const char* pathname, struct stat* buf) override;
  SysCallIntResult setsockopt(os_fd_t sockfd, int level, int optname, const void* optval,
                              socklen_t optlen) override;
  SysCallIntResult getsockopt(os_fd_t sockfd, int level, int optname, void* optval,
                              socklen_t* optlen) override;
  SysCallSocketResult socket(int domain, int type, int protocol) override;
  SysCallSizeResult sendmsg(os_fd_t fd, const msghdr* message, int flags) override;
  SysCallIntResult getsockname(os_fd_t sockfd, sockaddr* addr, socklen_t* addrlen) override;
  SysCallIntResult gethostname(char* name, size_t length) override;
  SysCallIntResult getpeername(os_fd_t sockfd, sockaddr* name, socklen_t* namelen) override;
  SysCallIntResult setsocketblocking(os_fd_t sockfd, bool blocking) override;
  SysCallIntResult connect(os_fd_t sockfd, const sockaddr* addr, socklen_t addrlen) override;
  SysCallIntResult open(const char* pathname, int flags) const override;
  SysCallIntResult open(const char* pathname, int flags, mode_t mode) const override;
  SysCallIntResult unlink(const char* pathname) const override;
  SysCallIntResult linkat(os_fd_t olddirfd, const char* oldpath, os_fd_t newdirfd,
                          const char* newpath, int flags) const override;
  SysCallIntResult mkstemp(char* tmplate) const override;
  bool supportsAllPosixFileOperations() const override;
  SysCallIntResult shutdown(os_fd_t sockfd, int how) override;
  SysCallIntResult socketpair(int domain, int type, int protocol, os_fd_t sv[2]) override;
  SysCallIntResult listen(os_fd_t sockfd, int backlog) override;
  SysCallSizeResult write(os_fd_t socket, const void* buffer, size_t length) override;
  SysCallSocketResult duplicate(os_fd_t oldfd) override;
  SysCallSocketResult accept(os_fd_t socket, sockaddr* addr, socklen_t* addrlen) override;
  SysCallBoolResult socketTcpInfo(os_fd_t sockfd, EnvoyTcpInfo* tcp_info) override;
  bool supportsGetifaddrs() const override;
  SysCallIntResult getifaddrs(InterfaceAddressVector& interfaces) override;
  SysCallIntResult getaddrinfo(const char* node, const char* service, const addrinfo* hints,
                               addrinfo** res) override;
  void freeaddrinfo(addrinfo* res) override;
};

using OsSysCallsSingleton = ThreadSafeSingleton<OsSysCallsImpl>;

} // namespace Api
} // namespace Envoy
