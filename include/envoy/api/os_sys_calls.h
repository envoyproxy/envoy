#pragma once

#include <sys/stat.h>

#include <chrono>
#include <memory>
#include <string>

#include "envoy/api/os_sys_calls_common.h"
#include "envoy/common/platform.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Api {

struct EnvoyTcpInfo {
  std::chrono::microseconds tcpi_rtt;
};

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
   * @see ioctl (man 2 ioctl)
   */
  virtual SysCallIntResult ioctl(os_fd_t sockfd, unsigned long int request, void* argp) PURE;

  /**
   * @see writev (man 2 writev)
   */
  virtual SysCallSizeResult writev(os_fd_t fd, const iovec* iov, int num_iov) PURE;

  /**
   * @see readv (man 2 readv)
   */
  virtual SysCallSizeResult readv(os_fd_t fd, const iovec* iov, int num_iov) PURE;

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
   * return true if the OS support both IP_TRANSPARENT and IPV6_TRANSPARENT options
   */
  virtual bool supportsIpTransparent() const PURE;

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
};

using OsSysCallsPtr = std::unique_ptr<OsSysCalls>;

} // namespace Api
} // namespace Envoy
