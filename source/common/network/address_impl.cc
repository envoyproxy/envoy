#include "common/network/address_impl.h"

#include <arpa/inet.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <array>
#include <cstdint>
#include <string>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"

namespace Envoy {
namespace Network {
namespace Address {

Address::InstanceConstSharedPtr addressFromSockAddr(const sockaddr_storage& ss, socklen_t ss_len,
                                                    bool v6only) {
  RELEASE_ASSERT(ss_len == 0 || ss_len >= sizeof(sa_family_t));
  switch (ss.ss_family) {
  case AF_INET: {
    RELEASE_ASSERT(ss_len == 0 || ss_len == sizeof(sockaddr_in));
    const struct sockaddr_in* sin = reinterpret_cast<const struct sockaddr_in*>(&ss);
    ASSERT(AF_INET == sin->sin_family);
    return std::make_shared<Address::Ipv4Instance>(sin);
  }
  case AF_INET6: {
    RELEASE_ASSERT(ss_len == 0 || ss_len == sizeof(sockaddr_in6));
    const struct sockaddr_in6* sin6 = reinterpret_cast<const struct sockaddr_in6*>(&ss);
    ASSERT(AF_INET6 == sin6->sin6_family);
    if (!v6only && IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
#ifndef s6_addr32
#ifdef __APPLE__
#define s6_addr32 __u6_addr.__u6_addr32
#endif
#endif
      struct sockaddr_in sin = {.sin_family = AF_INET,
                                .sin_port = sin6->sin6_port,
                                .sin_addr = {.s_addr = sin6->sin6_addr.s6_addr32[3]},
                                .sin_zero = {}};
      return std::make_shared<Address::Ipv4Instance>(&sin);
    } else {
      return std::make_shared<Address::Ipv6Instance>(*sin6, v6only);
    }
  }
  case AF_UNIX: {
    const struct sockaddr_un* sun = reinterpret_cast<const struct sockaddr_un*>(&ss);
    ASSERT(AF_UNIX == sun->sun_family);
    RELEASE_ASSERT(ss_len == 0 || ss_len >= offsetof(struct sockaddr_un, sun_path) + 1);
    return std::make_shared<Address::PipeInstance>(sun, ss_len);
  }
  default:
    throw EnvoyException(fmt::format("Unexpected sockaddr family: {}", ss.ss_family));
  }
  NOT_REACHED;
}

InstanceConstSharedPtr addressFromFd(int fd) {
  sockaddr_storage ss;
  socklen_t ss_len = sizeof ss;
  int rc = ::getsockname(fd, reinterpret_cast<sockaddr*>(&ss), &ss_len);
  if (rc != 0) {
    throw EnvoyException(
        fmt::format("getsockname failed for '{}': ({}) {}", fd, errno, strerror(errno)));
  }
  int socket_v6only = 0;
  if (ss.ss_family == AF_INET6) {
    socklen_t size_int = sizeof(socket_v6only);
    RELEASE_ASSERT(::getsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &socket_v6only, &size_int) == 0);
  }
  return addressFromSockAddr(ss, ss_len, rc == 0 && socket_v6only);
}

InstanceConstSharedPtr peerAddressFromFd(int fd) {
  sockaddr_storage ss;
  socklen_t ss_len = sizeof ss;
  const int rc = ::getpeername(fd, reinterpret_cast<sockaddr*>(&ss), &ss_len);
  if (rc != 0) {
    throw EnvoyException(fmt::format("getpeername failed for '{}': {}", fd, strerror(errno)));
  }
#ifdef __APPLE__
  if (ss_len == sizeof(sockaddr) && ss.ss_family == AF_UNIX) {
#else
  if (ss_len == sizeof(sa_family_t) && ss.ss_family == AF_UNIX) {
#endif
    // For Unix domain sockets, can't find out the peer name, but it should match our own
    // name for the socket (i.e. the path should match, barring any namespace or other
    // mechanisms to hide things, of which there are many).
    ss_len = sizeof ss;
    const int rc = ::getsockname(fd, reinterpret_cast<sockaddr*>(&ss), &ss_len);
    if (rc != 0) {
      throw EnvoyException(fmt::format("getsockname failed for '{}': {}", fd, strerror(errno)));
    }
  }
  return addressFromSockAddr(ss, ss_len);
}

int InstanceBase::socketFromSocketType(SocketType socketType) const {
#if defined(__APPLE__)
  int flags = 0;
#else
  int flags = SOCK_NONBLOCK;
#endif

  if (socketType == SocketType::Stream) {
    flags |= SOCK_STREAM;
  } else {
    flags |= SOCK_DGRAM;
  }

  int domain;
  if (type() == Type::Ip) {
    IpVersion version = ip()->version();
    if (version == IpVersion::v6) {
      domain = AF_INET6;
    } else {
      ASSERT(version == IpVersion::v4);
      domain = AF_INET;
    }
  } else {
    ASSERT(type() == Type::Pipe);
    domain = AF_UNIX;
  }

  int fd = ::socket(domain, flags, 0);
  RELEASE_ASSERT(fd != -1);

#ifdef __APPLE__
  // Cannot set SOCK_NONBLOCK as a ::socket flag.
  RELEASE_ASSERT(fcntl(fd, F_SETFL, O_NONBLOCK) != -1);
#endif

  return fd;
}

Ipv4Instance::Ipv4Instance(const sockaddr_in* address) : InstanceBase(Type::Ip) {
  ip_.ipv4_.address_ = *address;
  char str[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &address->sin_addr, str, INET_ADDRSTRLEN);
  friendly_name_ = fmt::format("{}:{}", str, ntohs(address->sin_port));
  ip_.friendly_address_ = str;
}

Ipv4Instance::Ipv4Instance(const std::string& address) : Ipv4Instance(address, 0) {}

Ipv4Instance::Ipv4Instance(const std::string& address, uint32_t port) : InstanceBase(Type::Ip) {
  memset(&ip_.ipv4_.address_, 0, sizeof(ip_.ipv4_.address_));
  ip_.ipv4_.address_.sin_family = AF_INET;
  ip_.ipv4_.address_.sin_port = htons(port);
  int rc = inet_pton(AF_INET, address.c_str(), &ip_.ipv4_.address_.sin_addr);
  if (1 != rc) {
    throw EnvoyException(fmt::format("invalid ipv4 address '{}'", address));
  }

  friendly_name_ = fmt::format("{}:{}", address, port);
  ip_.friendly_address_ = address;
}

Ipv4Instance::Ipv4Instance(uint32_t port) : InstanceBase(Type::Ip) {
  memset(&ip_.ipv4_.address_, 0, sizeof(ip_.ipv4_.address_));
  ip_.ipv4_.address_.sin_family = AF_INET;
  ip_.ipv4_.address_.sin_port = htons(port);
  ip_.ipv4_.address_.sin_addr.s_addr = INADDR_ANY;
  friendly_name_ = fmt::format("0.0.0.0:{}", port);
  ip_.friendly_address_ = "0.0.0.0";
}

int Ipv4Instance::bind(int fd) const {
  return ::bind(fd, reinterpret_cast<const sockaddr*>(&ip_.ipv4_.address_),
                sizeof(ip_.ipv4_.address_));
}

int Ipv4Instance::connect(int fd) const {
  return ::connect(fd, reinterpret_cast<const sockaddr*>(&ip_.ipv4_.address_),
                   sizeof(ip_.ipv4_.address_));
}

int Ipv4Instance::socket(SocketType type) const { return socketFromSocketType(type); }

absl::uint128 Ipv6Instance::Ipv6Helper::address() const {
  absl::uint128 result{0};
  static_assert(sizeof(absl::uint128) == 16, "The size of asbl::uint128 is not 16.");
  memcpy(&result, &address_.sin6_addr.s6_addr, sizeof(absl::uint128));
  return result;
}

uint32_t Ipv6Instance::Ipv6Helper::port() const { return ntohs(address_.sin6_port); }

std::string Ipv6Instance::Ipv6Helper::makeFriendlyAddress() const {
  char str[INET6_ADDRSTRLEN];
  const char* ptr = inet_ntop(AF_INET6, &address_.sin6_addr, str, INET6_ADDRSTRLEN);
  ASSERT(str == ptr);
  return ptr;
}

Ipv6Instance::Ipv6Instance(const sockaddr_in6& address, bool v6only) : InstanceBase(Type::Ip) {
  ip_.ipv6_.address_ = address;
  ip_.friendly_address_ = ip_.ipv6_.makeFriendlyAddress();
  ip_.v6only_ = v6only;
  friendly_name_ = fmt::format("[{}]:{}", ip_.friendly_address_, ip_.port());
}

Ipv6Instance::Ipv6Instance(const std::string& address) : Ipv6Instance(address, 0) {}

Ipv6Instance::Ipv6Instance(const std::string& address, uint32_t port) : InstanceBase(Type::Ip) {
  ip_.ipv6_.address_.sin6_family = AF_INET6;
  ip_.ipv6_.address_.sin6_port = htons(port);
  if (!address.empty()) {
    if (1 != inet_pton(AF_INET6, address.c_str(), &ip_.ipv6_.address_.sin6_addr)) {
      throw EnvoyException(fmt::format("invalid ipv6 address '{}'", address));
    }
  } else {
    ip_.ipv6_.address_.sin6_addr = in6addr_any;
  }
  // Just in case address is in a non-canonical format, format from network address.
  ip_.friendly_address_ = ip_.ipv6_.makeFriendlyAddress();
  friendly_name_ = fmt::format("[{}]:{}", ip_.friendly_address_, ip_.port());
}

Ipv6Instance::Ipv6Instance(uint32_t port) : Ipv6Instance("", port) {}

int Ipv6Instance::bind(int fd) const {
  return ::bind(fd, reinterpret_cast<const sockaddr*>(&ip_.ipv6_.address_),
                sizeof(ip_.ipv6_.address_));
}

int Ipv6Instance::connect(int fd) const {
  return ::connect(fd, reinterpret_cast<const sockaddr*>(&ip_.ipv6_.address_),
                   sizeof(ip_.ipv6_.address_));
}

int Ipv6Instance::socket(SocketType type) const {
  const int fd = socketFromSocketType(type);

  // Setting IPV6_V6ONLY resticts the IPv6 socket to IPv6 connections only.
  const int v6only = ip_.v6only_;
  RELEASE_ASSERT(::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)) != -1);
  return fd;
}

PipeInstance::PipeInstance(const sockaddr_un* address, socklen_t ss_len)
    : InstanceBase(Type::Pipe) {
  if (address->sun_path[0] == '\0') {
#if !defined(__linux__)
    throw EnvoyException("Abstract AF_UNIX sockets are only supported on linux.");
#endif
    RELEASE_ASSERT(ss_len >= offsetof(struct sockaddr_un, sun_path) + 1);
    abstract_namespace_ = true;
    address_length_ = ss_len - offsetof(struct sockaddr_un, sun_path);
  }
  address_ = *address;
  friendly_name_ =
      abstract_namespace_
          ? fmt::format("@{}", absl::string_view(address_.sun_path + 1, address_length_ - 1))
          : address_.sun_path;
}

PipeInstance::PipeInstance(const std::string& pipe_path) : InstanceBase(Type::Pipe) {
  memset(&address_, 0, sizeof(address_));
  address_.sun_family = AF_UNIX;
  StringUtil::strlcpy(&address_.sun_path[0], pipe_path.c_str(), sizeof(address_.sun_path));
  friendly_name_ = address_.sun_path;
  if (address_.sun_path[0] == '@') {
#if !defined(__linux__)
    throw EnvoyException("Abstract AF_UNIX sockets are only supported on linux.");
#endif
    abstract_namespace_ = true;
    address_length_ = strlen(address_.sun_path);
    address_.sun_path[0] = '\0';
  }
}

int PipeInstance::bind(int fd) const {
  if (abstract_namespace_) {
    return ::bind(fd, reinterpret_cast<const sockaddr*>(&address_),
                  offsetof(struct sockaddr_un, sun_path) + address_length_);
  }
  return ::bind(fd, reinterpret_cast<const sockaddr*>(&address_), sizeof(address_));
}

int PipeInstance::connect(int fd) const {
  if (abstract_namespace_) {
    return ::connect(fd, reinterpret_cast<const sockaddr*>(&address_),
                     offsetof(struct sockaddr_un, sun_path) + address_length_);
  }
  return ::connect(fd, reinterpret_cast<const sockaddr*>(&address_), sizeof(address_));
}

int PipeInstance::socket(SocketType type) const { return socketFromSocketType(type); }

} // namespace Address
} // namespace Network
} // namespace Envoy
