#pragma once

#include <sys/stat.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/api/os_sys_calls_common.h"
#include "envoy/common/platform.h"
#include "envoy/common/pure.h"
#include "envoy/network/address.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Api {

struct EnvoyTcpInfo {
  std::chrono::microseconds tcpi_rtt;
  // Congestion window, in bytes. Note that posix's TCP_INFO socket option returns cwnd in packets,
  // we multiply it by MSS to get bytes.
  uint32_t tcpi_snd_cwnd = 0;
};

// Small struct to avoid exposing ifaddrs -- which is not defined in all platforms -- to the
// codebase.
struct InterfaceAddress {
  InterfaceAddress(absl::string_view interface_name, unsigned int interface_flags,
                   Envoy::Network::Address::InstanceConstSharedPtr interface_addr)
      : interface_name_(interface_name), interface_flags_(interface_flags),
        interface_addr_(interface_addr) {}

  std::string interface_name_;
  unsigned int interface_flags_;
  Envoy::Network::Address::InstanceConstSharedPtr interface_addr_;
};

using InterfaceAddressVector = std::vector<InterfaceAddress>;

class OsSysCalls {
public:
  virtual ~OsSysCalls() = default;

  /**
   * @see bind (man 2 bind)
   */
  virtual SysCallIntResult bind(os_fd_t sockfd, const sockaddr* addr, socklen_t addrlen) PURE;

  /**
   * @see chmod (man 2 chmod)
   */
  virtual SysCallIntResult chmod(const std::string& path, mode_t mode) PURE;

  /**
   * This interface is based on Windows `WSAIoctl`. It becomes equivalent with the POSIX interface
   * with `in_buffer` as `argp` and the rest of the parameters ignored.
   * @see ioctl (man 2 ioctl)
   * @see WSAIoctl (MSDN)
   */
  virtual SysCallIntResult ioctl(os_fd_t sockfd, unsigned long control_code, void* in_buffer,
                                 unsigned long in_buffer_len, void* out_buffer,
                                 unsigned long out_buffer_len, unsigned long* bytes_returned) PURE;

  /**
   * @see writev (man 2 writev)
   */
  virtual SysCallSizeResult writev(os_fd_t fd, const iovec* iov, int num_iov) PURE;

  /**
   * @see readv (man 2 readv)
   */
  virtual SysCallSizeResult readv(os_fd_t fd, const iovec* iov, int num_iov) PURE;

  /**
   * @see man 2 pwrite
   */
  virtual SysCallSizeResult pwrite(os_fd_t fd, const void* buffer, size_t length,
                                   off_t offset) const PURE;

  /**
   * @see man 2 pread
   */
  virtual SysCallSizeResult pread(os_fd_t fd, void* buffer, size_t length, off_t offset) const PURE;

  /**
   * @see send (man 2 send)
   */
  virtual SysCallSizeResult send(os_fd_t socket, void* buffer, size_t length, int flags) PURE;

  /**
   * @see recv (man 2 recv)
   */
  virtual SysCallSizeResult recv(os_fd_t socket, void* buffer, size_t length, int flags) PURE;

  /**
   * @see recvmsg (man 2 recvmsg)
   */
  virtual SysCallSizeResult recvmsg(os_fd_t sockfd, msghdr* msg, int flags) PURE;

  /**
   * @see recvmmsg (man 2 recvmmsg)
   */
  virtual SysCallIntResult recvmmsg(os_fd_t sockfd, struct mmsghdr* msgvec, unsigned int vlen,
                                    int flags, struct timespec* timeout) PURE;

  /**
   * return true if the OS supports recvmmsg() and sendmmsg().
   */
  virtual bool supportsMmsg() const PURE;

  /**
   * return true if the OS supports UDP GRO.
   */
  virtual bool supportsUdpGro() const PURE;

  /**
   * return true if the OS supports UDP GSO
   */
  virtual bool supportsUdpGso() const PURE;

  /**
   * return true if the OS support IP_TRANSPARENT or IPV6_TRANSPARENT options by the ip version.
   */
  virtual bool supportsIpTransparent(Network::Address::IpVersion version) const PURE;

  /**
   * return true if the OS supports multi-path TCP
   */
  virtual bool supportsMptcp() const PURE;

  /**
   * Release all resources allocated for fd.
   * @return zero on success, -1 returned otherwise.
   */
  virtual SysCallIntResult close(os_fd_t fd) PURE;

  /**
   * @see man 2 ftruncate
   */
  virtual SysCallIntResult ftruncate(int fd, off_t length) PURE;

  /**
   * @see man 2 mmap
   */
  virtual SysCallPtrResult mmap(void* addr, size_t length, int prot, int flags, int fd,
                                off_t offset) PURE;

  /**
   * @see man 2 stat
   */
  virtual SysCallIntResult stat(const char* pathname, struct stat* buf) PURE;

  /**
   * @see man 2 fstat
   */
  virtual SysCallIntResult fstat(os_fd_t fd, struct stat* buf) PURE;

  /**
   * @see man 2 setsockopt
   */
  virtual SysCallIntResult setsockopt(os_fd_t sockfd, int level, int optname, const void* optval,
                                      socklen_t optlen) PURE;

  /**
   * @see man 2 getsockopt
   */
  virtual SysCallIntResult getsockopt(os_fd_t sockfd, int level, int optname, void* optval,
                                      socklen_t* optlen) PURE;

  /**
   * @see man 2 socket
   */
  virtual SysCallSocketResult socket(int domain, int type, int protocol) PURE;

  /**
   * @see man 2 sendmsg
   */
  virtual SysCallSizeResult sendmsg(os_fd_t sockfd, const msghdr* message, int flags) PURE;

  /**
   * @see man 2 getsockname
   */
  virtual SysCallIntResult getsockname(os_fd_t sockfd, sockaddr* addr, socklen_t* addrlen) PURE;

  /**
   * @see man 2 gethostname
   */
  virtual SysCallIntResult gethostname(char* name, size_t length) PURE;

  /**
   * @see man 2 getpeername
   */
  virtual SysCallIntResult getpeername(os_fd_t sockfd, sockaddr* name, socklen_t* namelen) PURE;

  /**
   * Toggle the blocking state bit using fcntl
   */
  virtual SysCallIntResult setsocketblocking(os_fd_t sockfd, bool blocking) PURE;

  /**
   * @see man 2 connect
   */
  virtual SysCallIntResult connect(os_fd_t sockfd, const sockaddr* addr, socklen_t addrlen) PURE;

  /**
   * @see man 2 open
   */
  virtual SysCallIntResult open(const char* pathname, int flags) const PURE;

  /**
   * @see man 2 open
   */
  virtual SysCallIntResult open(const char* pathname, int flags, mode_t mode) const PURE;

  /**
   * @see man 2 unlink
   */
  virtual SysCallIntResult unlink(const char* pathname) const PURE;

  /**
   * @see man 2 unlink
   */
  virtual SysCallIntResult linkat(os_fd_t olddirfd, const char* oldpath, os_fd_t newdirfd,
                                  const char* newpath, int flags) const PURE;

  /**
   * @see man 2 mkstemp
   */
  virtual SysCallIntResult mkstemp(char* tmplate) const PURE;

  /**
   * Returns true if mkstemp, linkat, unlink, open, close, pread and pwrite are fully supported.
   */
  virtual bool supportsAllPosixFileOperations() const PURE;

  /**
   * @see man 2 shutdown
   */
  virtual SysCallIntResult shutdown(os_fd_t sockfd, int how) PURE;

  /**
   * @see man 2 socketpair
   */
  virtual SysCallIntResult socketpair(int domain, int type, int protocol, os_fd_t sv[2]) PURE;

  /**
   * @see man 2 listen
   */
  virtual SysCallIntResult listen(os_fd_t sockfd, int backlog) PURE;

  /**
   * @see man 2 write
   */
  virtual SysCallSizeResult write(os_fd_t socket, const void* buffer, size_t length) PURE;

  /**
   * @see man 2 accept. The fds returned are configured to be non-blocking.
   */
  virtual SysCallSocketResult accept(os_fd_t socket, sockaddr* addr, socklen_t* addrlen) PURE;

  /**
   * @see man 2 dup(2).
   */
  virtual SysCallSocketResult duplicate(os_fd_t oldfd) PURE;

  /**
   * @see man TCP_INFO. Get the tcp info for the socket.
   */
  virtual SysCallBoolResult socketTcpInfo(os_fd_t sockfd, EnvoyTcpInfo* tcp_info) PURE;

  /**
   * return true if the OS supports getifaddrs.
   */
  virtual bool supportsGetifaddrs() const PURE;

  /**
   * @see man getifaddrs
   */
  virtual SysCallIntResult getifaddrs(InterfaceAddressVector& interfaces) PURE;

  /**
   * @see man getaddrinfo
   */
  virtual SysCallIntResult getaddrinfo(const char* node, const char* service, const addrinfo* hints,
                                       addrinfo** res) PURE;

  /**
   * @see man freeaddrinfo
   */
  virtual void freeaddrinfo(addrinfo* res) PURE;
};

using OsSysCallsPtr = std::unique_ptr<OsSysCalls>;

} // namespace Api
} // namespace Envoy
