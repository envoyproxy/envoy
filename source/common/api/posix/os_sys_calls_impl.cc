#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <string>

#include "envoy/network/socket.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/network/address_impl.h"

#if defined(__ANDROID_API__) && __ANDROID_API__ < 24
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
namespace android {
#include "third_party/android/ifaddrs-android.h"
} // namespace android
#pragma clang diagnostic pop
#endif

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

SysCallSizeResult OsSysCallsImpl::pwrite(os_fd_t fd, const void* buffer, size_t length,
                                         off_t offset) const {
  const ssize_t rc = ::pwrite(fd, buffer, length, offset);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallSizeResult OsSysCallsImpl::pread(os_fd_t fd, void* buffer, size_t length,
                                        off_t offset) const {
  const ssize_t rc = ::pread(fd, buffer, length, offset);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallSizeResult OsSysCallsImpl::send(os_fd_t socket, void* buffer, size_t length, int flags) {
  const ssize_t rc = ::send(socket, buffer, length, flags);
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
  return {false, EOPNOTSUPP};
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
    int optval;
    socklen_t optlen = sizeof(optval);
    bool result = (0 <= ::getsockopt(fd, IPPROTO_UDP, UDP_SEGMENT, &optval, &optlen));
    ::close(fd);
    return result;
  }();
  return is_supported;
#endif
}

bool OsSysCallsImpl::supportsIpTransparent(Network::Address::IpVersion ip_version) const {
#if !defined(__linux__)
  UNREFERENCED_PARAMETER(ip_version);
  return false;
#else
  // The linux kernel supports IP_TRANSPARENT by following patch(starting from v2.6.28) :
  // https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/net/ipv4/ip_sockglue.c?id=f5715aea4564f233767ea1d944b2637a5fd7cd2e
  //
  // The linux kernel supports IPV6_TRANSPARENT by following patch(starting from v2.6.37) :
  // https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/net/ipv6/ipv6_sockglue.c?id=6c46862280c5f55eda7750391bc65cd7e08c7535
  //
  // So, almost recent linux kernel supports both IP_TRANSPARENT and IPV6_TRANSPARENT options.
  // But there are also has ipv4 only or ipv6 only scenarios.
  //
  // And these socket options need CAP_NET_ADMIN capability to be applied.
  // The CAP_NET_ADMIN capability should be applied by root user before call this function.

  static constexpr auto transparent_supported = [](int family) {
    auto opt_tp = family == AF_INET ? ENVOY_SOCKET_IP_TRANSPARENT : ENVOY_SOCKET_IPV6_TRANSPARENT;
    int fd = ::socket(family, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
    int val = 1;
    bool result = (0 == ::setsockopt(fd, opt_tp.level(), opt_tp.option(), &val, sizeof(val)));
    ::close(fd);
    return result;
  };
  // Check ipv4 case
  static const bool ipv4_is_supported = transparent_supported(AF_INET);
  // Check ipv6 case
  static const bool ipv6_is_supported = transparent_supported(AF_INET6);
  return ip_version == Network::Address::IpVersion::v4 ? ipv4_is_supported : ipv6_is_supported;
#endif
}

bool OsSysCallsImpl::supportsMptcp() const {
#if !defined(__linux__)
  return false;
#else
  int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_MPTCP);
  if (fd < 0) {
    return false;
  }

  ::close(fd);
  return true;
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

SysCallIntResult OsSysCallsImpl::fstat(os_fd_t fd, struct stat* buf) {
  const int rc = ::fstat(fd, buf);
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

SysCallIntResult OsSysCallsImpl::open(const char* pathname, int flags) const {
  const int rc = ::open(pathname, flags);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallIntResult OsSysCallsImpl::open(const char* pathname, int flags, mode_t mode) const {
  const int rc = ::open(pathname, flags, mode);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallIntResult OsSysCallsImpl::unlink(const char* pathname) const {
  const int rc = ::unlink(pathname);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallIntResult OsSysCallsImpl::linkat(os_fd_t olddirfd, const char* oldpath, os_fd_t newdirfd,
                                        const char* newpath, int flags) const {
  const int rc = ::linkat(olddirfd, oldpath, newdirfd, newpath, flags);
  return {rc, rc != -1 ? 0 : errno};
}

SysCallIntResult OsSysCallsImpl::mkstemp(char* tmplate) const {
  const int rc = ::mkstemp(tmplate);
  return {rc, rc != -1 ? 0 : errno};
}

bool OsSysCallsImpl::supportsAllPosixFileOperations() const { return true; }

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
#else
  rc = ::accept(sockfd, addr, addrlen);
  if (rc >= 0) {
    setsocketblocking(rc, false);
  }
#endif
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

    const uint64_t mss = (unix_tcp_info.tcpi_snd_mss > 0) ? unix_tcp_info.tcpi_snd_mss : 1460;
    // Convert packets to bytes.
    tcp_info->tcpi_snd_cwnd = unix_tcp_info.tcpi_snd_cwnd * mss;
  }
  return {!SOCKET_FAILURE(result), !SOCKET_FAILURE(result) ? 0 : errno};
#else
  return {false, EOPNOTSUPP};
#endif
}

bool OsSysCallsImpl::supportsGetifaddrs() const { return true; }

SysCallIntResult OsSysCallsImpl::getifaddrs([[maybe_unused]] InterfaceAddressVector& interfaces) {
#if defined(__ANDROID_API__) && __ANDROID_API__ < 24
  struct android::ifaddrs* ifaddr;
  struct android::ifaddrs* ifa;

  const int rc = android::getifaddrs(&ifaddr);
  if (rc == -1) {
    return {rc, errno};
  }

  for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr) {
      continue;
    }

    if (ifa->ifa_addr->sa_family == AF_INET || ifa->ifa_addr->sa_family == AF_INET6) {
      const sockaddr_storage* ss = reinterpret_cast<sockaddr_storage*>(ifa->ifa_addr);
      size_t ss_len =
          ifa->ifa_addr->sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
      StatusOr<Network::Address::InstanceConstSharedPtr> address =
          Network::Address::addressFromSockAddr(*ss, ss_len, ifa->ifa_addr->sa_family == AF_INET6);
      if (address.ok()) {
        interfaces.emplace_back(ifa->ifa_name, ifa->ifa_flags, *address);
      }
    }
  }

  if (ifaddr) {
    android::freeifaddrs(ifaddr);
  }
  return {rc, 0};
#else
  struct ifaddrs* ifaddr;
  struct ifaddrs* ifa;

  const int rc = ::getifaddrs(&ifaddr);
  if (rc == -1) {
    return {rc, errno};
  }

  for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr) {
      continue;
    }

    if (ifa->ifa_addr->sa_family == AF_INET || ifa->ifa_addr->sa_family == AF_INET6) {
      const sockaddr_storage* ss = reinterpret_cast<sockaddr_storage*>(ifa->ifa_addr);
      size_t ss_len =
          ifa->ifa_addr->sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
      StatusOr<Network::Address::InstanceConstSharedPtr> address =
          Network::Address::addressFromSockAddr(*ss, ss_len, ifa->ifa_addr->sa_family == AF_INET6);
      if (address.ok()) {
        interfaces.emplace_back(ifa->ifa_name, ifa->ifa_flags, *address);
      }
    }
  }

  if (ifaddr) {
    ::freeifaddrs(ifaddr);
  }

  return {rc, 0};
#endif
}

SysCallIntResult OsSysCallsImpl::getaddrinfo(const char* node, const char* service,
                                             const addrinfo* hints, addrinfo** res) {
  const int rc = ::getaddrinfo(node, service, hints, res);
  return {rc, errno};
}

void OsSysCallsImpl::freeaddrinfo(addrinfo* res) { ::freeaddrinfo(res); }

} // namespace Api
} // namespace Envoy
