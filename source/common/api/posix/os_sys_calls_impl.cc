#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <string>

#include "source/common/api/os_sys_calls_impl.h"

namespace Envoy {
namespace Api {

SysCallIntResult OsSysCallsImpl::bind(os_fd_t sockfd, const sockaddr* addr, socklen_t addrlen) {
  const int rc = ::bind(sockfd, addr, addrlen);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallIntResult OsSysCallsImpl::chmod(const std::string& path, mode_t mode) {
  const int rc = ::chmod(path.c_str(), mode);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallIntResult OsSysCallsImpl::ioctl(os_fd_t sockfd, unsigned long request, void* argp,
                                       unsigned long, void*, unsigned long, unsigned long*) {
  const int rc = ::ioctl(sockfd, request, argp);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallIntResult OsSysCallsImpl::close(os_fd_t fd) {
  const int rc = ::close(fd);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallSizeResult OsSysCallsImpl::writev(os_fd_t fd, const iovec* iov, int num_iov) {
  const ssize_t rc = ::writev(fd, iov, num_iov);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallSizeResult OsSysCallsImpl::readv(os_fd_t fd, const iovec* iov, int num_iov) {
  const ssize_t rc = ::readv(fd, iov, num_iov);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallSizeResult OsSysCallsImpl::recv(os_fd_t socket, void* buffer, size_t length, int flags) {
  const ssize_t rc = ::recv(socket, buffer, length, flags);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallSizeResult OsSysCallsImpl::recvmsg(os_fd_t sockfd, msghdr* msg, int flags) {
  const ssize_t rc = ::recvmsg(sockfd, msg, flags);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallIntResult OsSysCallsImpl::recvmmsg(os_fd_t sockfd, struct mmsghdr* msgvec, unsigned int vlen,
                                          int flags, struct timespec* timeout) {
#if ENVOY_MMSG_MORE
  const int rc = ::recvmmsg(sockfd, msgvec, vlen, flags, timeout);
  return {rc, errno};
#else
  UNREFERENCED_PARAMETER(sockfd);
  UNREFERENCED_PARAMETER(msgvec);
  UNREFERENCED_PARAMETER(vlen);
  UNREFERENCED_PARAMETER(flags);
  UNREFERENCED_PARAMETER(timeout);
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
#endif
}

bool OsSysCallsImpl::supportsMmsg() const {
#if ENVOY_MMSG_MORE
  return true;
#else
  return false;
#endif
}

bool OsSysCallsImpl::supportsUdpGro() const {
#if !defined(__linux__)
  return false;
#else
  static const bool is_supported = [] {
    int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
    if (fd < 0) {
      return false;
    }
    int val = 1;
    bool result = (0 == ::setsockopt(fd, IPPROTO_UDP, UDP_GRO, &val, sizeof(val)));
    ::close(fd);
    return result;
  }();
  return is_supported;
#endif
}

bool OsSysCallsImpl::supportsUdpGso() const {
#if !defined(__linux__)
  return false;
#else
  static const bool is_supported = [] {
    int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
    if (fd < 0) {
      return false;
    }
    int optval;
    socklen_t optlen = sizeof(optval);
    bool result = (0 <= ::getsockopt(fd, IPPROTO_UDP, UDP_SEGMENT, &optval, &optlen));
    ::close(fd);
    return result;
  }();
  return is_supported;
#endif
}

bool OsSysCallsImpl::supportsIpTransparent() const {
#if !defined(__linux__) || !defined(IPV6_TRANSPARENT)
  return false;
#else
  // The linux kernel supports IP_TRANSPARENT by following patch(starting from v2.6.28) :
  // https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/net/ipv4/ip_sockglue.c?id=f5715aea4564f233767ea1d944b2637a5fd7cd2e
  //
  // The linux kernel supports IPV6_TRANSPARENT by following patch(starting from v2.6.37) :
  // https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/net/ipv6/ipv6_sockglue.c?id=6c46862280c5f55eda7750391bc65cd7e08c7535
  //
  // So, almost recent linux kernel supports both IP_TRANSPARENT and IPV6_TRANSPARENT options.
  //
  // And these socket options need CAP_NET_ADMIN capability to be applied.
  // The CAP_NET_ADMIN capability should be applied by root user before call this function.
  static const bool is_supported = [] {
    // Check ipv4 case
    int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
    if (fd < 0) {
      return false;
    }
    int val = 1;
    bool result = (0 == ::setsockopt(fd, IPPROTO_IP, IP_TRANSPARENT, &val, sizeof(val)));
    ::close(fd);
    if (!result) {
      return false;
    }
    // Check ipv6 case
    fd = ::socket(AF_INET6, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
    if (fd < 0) {
      return false;
    }
    val = 1;
    result = (0 == ::setsockopt(fd, IPPROTO_IPV6, IPV6_TRANSPARENT, &val, sizeof(val)));
    ::close(fd);
    return result;
  }();
  return is_supported;
#endif
}

SysCallIntResult OsSysCallsImpl::ftruncate(int fd, off_t length) {
  const int rc = ::ftruncate(fd, length);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallPtrResult OsSysCallsImpl::mmap(void* addr, size_t length, int prot, int flags, int fd,
                                      off_t offset) {
  void* rc = ::mmap(addr, length, prot, flags, fd, offset);
  return {rc, rc != MAP_FAILED ? 0 : errno};
}

SysCallIntResult OsSysCallsImpl::stat(const char* pathname, struct stat* buf) {
  const int rc = ::stat(pathname, buf);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallIntResult OsSysCallsImpl::setsockopt(os_fd_t sockfd, int level, int optname,
                                            const void* optval, socklen_t optlen) {
  const int rc = ::setsockopt(sockfd, level, optname, optval, optlen);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallIntResult OsSysCallsImpl::getsockopt(os_fd_t sockfd, int level, int optname, void* optval,
                                            socklen_t* optlen) {
  const int rc = ::getsockopt(sockfd, level, optname, optval, optlen);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallSocketResult OsSysCallsImpl::socket(int domain, int type, int protocol) {
  const os_fd_t rc = ::socket(domain, type, protocol);
  return {rc, SOCKET_VALID(rc) ? 0 : errno};
}

SysCallSizeResult OsSysCallsImpl::sendmsg(os_fd_t fd, const msghdr* message, int flags) {
  const int rc = ::sendmsg(fd, message, flags);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallIntResult OsSysCallsImpl::getsockname(os_fd_t sockfd, sockaddr* addr, socklen_t* addrlen) {
  const int rc = ::getsockname(sockfd, addr, addrlen);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallIntResult OsSysCallsImpl::gethostname(char* name, size_t length) {
  const int rc = ::gethostname(name, length);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallIntResult OsSysCallsImpl::getpeername(os_fd_t sockfd, sockaddr* name, socklen_t* namelen) {
  const int rc = ::getpeername(sockfd, name, namelen);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallIntResult OsSysCallsImpl::setsocketblocking(os_fd_t sockfd, bool blocking) {
  const int flags = ::fcntl(sockfd, F_GETFL, 0);
  int rc;
  if (flags == -1) {
    return {-1, errno};
  }
  if (blocking) {
    rc = ::fcntl(sockfd, F_SETFL, flags & ~O_NONBLOCK);
  } else {
    rc = ::fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
  }
  return {rc, rc != -1 ? 0 : errno};
}

SysCallIntResult OsSysCallsImpl::connect(os_fd_t sockfd, const sockaddr* addr, socklen_t addrlen) {
  const int rc = ::connect(sockfd, addr, addrlen);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallIntResult OsSysCallsImpl::shutdown(os_fd_t sockfd, int how) {
  const int rc = ::shutdown(sockfd, how);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallIntResult OsSysCallsImpl::socketpair(int domain, int type, int protocol, os_fd_t sv[2]) {
  const int rc = ::socketpair(domain, type, protocol, sv);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallIntResult OsSysCallsImpl::listen(os_fd_t sockfd, int backlog) {
  const int rc = ::listen(sockfd, backlog);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallSizeResult OsSysCallsImpl::write(os_fd_t sockfd, const void* buffer, size_t length) {
  const ssize_t rc = ::write(sockfd, buffer, length);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallSocketResult OsSysCallsImpl::duplicate(os_fd_t oldfd) {
  const int rc = ::dup(oldfd);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallSocketResult OsSysCallsImpl::accept(os_fd_t sockfd, sockaddr* addr, socklen_t* addrlen) {
  os_fd_t rc;

#if defined(__linux__)
  rc = ::accept4(sockfd, addr, addrlen, SOCK_NONBLOCK);
  // If failed with EINVAL try without flags
  if (rc >= 0 || errno != EINVAL) {
    return {rc, rc != -1 ? 0 : errno};
  }
#endif

  rc = ::accept(sockfd, addr, addrlen);
  if (rc >= 0) {
    setsocketblocking(rc, false);
  }

  return {rc, rc != -1 ? 0 : errno};
}

SysCallBoolResult OsSysCallsImpl::socketTcpInfo([[maybe_unused]] os_fd_t sockfd,
                                                [[maybe_unused]] EnvoyTcpInfo* tcp_info) {
#ifdef TCP_INFO
  struct tcp_info unix_tcp_info;
  socklen_t len = sizeof(unix_tcp_info);
  auto result = ::getsockopt(sockfd, IPPROTO_TCP, TCP_INFO, &unix_tcp_info, &len);
  if (!SOCKET_FAILURE(result)) {
    tcp_info->tcpi_rtt = std::chrono::microseconds(unix_tcp_info.tcpi_rtt);
  }
  return {!SOCKET_FAILURE(result), !SOCKET_FAILURE(result) ? 0 : errno};
#endif

  return {false, EOPNOTSUPP};
}

} // namespace Api
} // namespace Envoy
