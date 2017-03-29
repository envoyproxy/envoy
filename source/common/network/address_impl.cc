#include "common/network/address_impl.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/utility.h"

namespace Network {
namespace Address {

Address::InstanceConstSharedPtr addressFromSockAddr(const sockaddr_storage& ss, socklen_t ss_len) {
  if (ss_len != 0 && ss_len <= sizeof(sa_family_t)) {
    throw EnvoyException(fmt::format("sockaddr_storage is too short: {}", ss_len));
  }
  switch (ss.ss_family) {
  case AF_INET:
    if (ss_len == 0 || ss_len == sizeof(sockaddr_in)) {
      const struct sockaddr_in* sin = reinterpret_cast<const struct sockaddr_in*>(&ss);
      // Only ASSERT-ing in debug builds because definition of sockaddr_storage and sockaddr_in
      // requires them to share the same storage for the ss_family and sin_family fields.
      ASSERT(AF_INET == sin->sin_family);
      return InstanceConstSharedPtr(new Address::Ipv4Instance(sin));
    } else {
      throw EnvoyException(
          fmt::format("sockaddr_in is wrong length: {} != {}", ss_len, sizeof(sockaddr_in)));
    }
  case AF_INET6:
    if (ss_len == 0 || ss_len == sizeof(sockaddr_in6)) {
      const struct sockaddr_in6* sin6 = reinterpret_cast<const struct sockaddr_in6*>(&ss);
      // Only ASSERT-ing in debug builds because definition of sockaddr_storage and sockaddr_in6
      // requires them to share the same storage for the ss_family and sin6_family fields.
      ASSERT(AF_INET6 == sin6->sin6_family);
      return InstanceConstSharedPtr(new Address::Ipv6Instance(*sin6));
    } else {
      throw EnvoyException(
          fmt::format("sockaddr_in6 is wrong length: {} != {}", ss_len, sizeof(sockaddr_in6)));
    }
  case AF_UNIX: {
    const struct sockaddr_un* sun = reinterpret_cast<const struct sockaddr_un*>(&ss);
    // Only ASSERT-ing in debug builds because definition of sockaddr_storage and sockaddr_un
    // requires them to share the same storage for the ss_family and sun_family fields.
    ASSERT(AF_UNIX == sun->sun_family);
    if (ss_len != 0) {
      // Note that we're not supporting unnamed or abstract AF_UNIX sockets, only those with a
      // pathname.
      if (ss_len <= offsetof(struct sockaddr_un, sun_path)) {
        throw EnvoyException(fmt::format("sockaddr_un not long enough for path: {}", ss_len));
      }
      size_t path_len = strlen(sun->sun_path);
      if (ss_len != offsetof(struct sockaddr_un, sun_path) + path_len + 1) {
        throw EnvoyException(
            fmt::format("sockaddr_un has wrong length not long enough for path: {}", ss_len));
      }
    }
    return InstanceConstSharedPtr(new Address::PipeInstance(sun));
  }
  default:
    throw EnvoyException(fmt::format("Unexpected sockaddr family: {}", ss.ss_family));
  }
  NOT_REACHED;
}

/**
 * Convert an address in the form of the socket address struct defined by Posix, Linux, etc. into
 * a Network::Address::Instance and return a pointer to it.  Raises an EnvoyException on failure.
 * @param addr a valid address with family AF_INET, AF_INET6 or AF_UNIX.
 * @return InstanceConstSharedPtr the address.
 */
Address::InstanceConstSharedPtr addressFromSockAddr(const sockaddr& addr) {
  return addressFromSockAddr(*reinterpret_cast<const struct sockaddr_storage*>(&addr), 0);
}

InstanceConstSharedPtr addressFromFd(int fd) {
  sockaddr_storage ss;
  socklen_t ss_len = sizeof ss;
  const int rc = ::getsockname(fd, reinterpret_cast<sockaddr*>(&ss), &ss_len);
  if (rc != 0) {
    throw EnvoyException(fmt::format("getsockname failed for '{}': {}", fd, strerror(errno)));
  }
  return addressFromSockAddr(ss, ss_len);
}

int InstanceBase::flagsFromSocketType(SocketType type) const {
  int flags = SOCK_NONBLOCK;
  if (type == SocketType::Stream) {
    flags |= SOCK_STREAM;
  } else {
    flags |= SOCK_DGRAM;
  }
  return flags;
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

int Ipv4Instance::socket(SocketType type) const {
  return ::socket(AF_INET, flagsFromSocketType(type), 0);
}

std::array<uint8_t, 16> Ipv6Instance::Ipv6Helper::address() const {
  std::array<uint8_t, 16> result;
  std::copy(std::begin(address_.sin6_addr.s6_addr), std::end(address_.sin6_addr.s6_addr),
            std::begin(result));
  return result;
}

uint32_t Ipv6Instance::Ipv6Helper::port() const { return ntohs(address_.sin6_port); }

std::string Ipv6Instance::Ipv6Helper::makeFriendlyAddress() const {
  char str[INET6_ADDRSTRLEN];
  const char* ptr = inet_ntop(AF_INET6, &address_.sin6_addr, str, INET6_ADDRSTRLEN);
  ASSERT(str == ptr);
  return ptr;
}

Ipv6Instance::Ipv6Instance(const sockaddr_in6& address) : InstanceBase(Type::Ip) {
  ip_.ipv6_.address_ = address;
  ip_.friendly_address_ = ip_.ipv6_.makeFriendlyAddress();
  friendly_name_ = fmt::format("[{}]:{}", ip_.friendly_address_, ip_.port());
}

Ipv6Instance::Ipv6Instance(const std::string& address) : Ipv6Instance(address, 0) {}

Ipv6Instance::Ipv6Instance(const std::string& address, uint32_t port) : InstanceBase(Type::Ip) {
  memset(&ip_.ipv6_.address_, 0, sizeof(ip_.ipv6_.address_));
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
  return ::socket(AF_INET6, flagsFromSocketType(type), 0);
}

PipeInstance::PipeInstance(const sockaddr_un* address) : InstanceBase(Type::Pipe) {
  if (address->sun_path[0] == '\0') {
    throw EnvoyException("Abstract AF_UNIX sockets not supported.");
  }
  address_ = *address;
  friendly_name_ = address_.sun_path;
}

PipeInstance::PipeInstance(const std::string& pipe_path) : InstanceBase(Type::Pipe) {
  memset(&address_, 0, sizeof(address_));
  address_.sun_family = AF_UNIX;
  StringUtil::strlcpy(&address_.sun_path[0], pipe_path.c_str(), sizeof(address_.sun_path));
  friendly_name_ = address_.sun_path;
}

int PipeInstance::bind(int fd) const {
  return ::bind(fd, reinterpret_cast<const sockaddr*>(&address_), sizeof(address_));
}

int PipeInstance::connect(int fd) const {
  return ::connect(fd, reinterpret_cast<const sockaddr*>(&address_), sizeof(address_));
}

int PipeInstance::socket(SocketType type) const {
  return ::socket(AF_UNIX, flagsFromSocketType(type), 0);
}

InstanceConstSharedPtr parseInternetAddress(const std::string& ip_addr) {
  sockaddr_in sa4;
  if (inet_pton(AF_INET, ip_addr.c_str(), &sa4.sin_addr) == 1) {
    sa4.sin_family = AF_INET;
    sa4.sin_port = 0;
    return InstanceConstSharedPtr(new Ipv4Instance(&sa4));
  }
  sockaddr_in6 sa6;
  if (inet_pton(AF_INET6, ip_addr.c_str(), &sa6.sin6_addr) == 1) {
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = 0;
    return InstanceConstSharedPtr(new Ipv6Instance(sa6));
  }
  return nullptr;
}

InstanceConstSharedPtr parseInternetAddressAndPort(const std::string& addr) {
  if (addr.empty()) {
    return nullptr;
  }
  if (addr[0] == '[') {
    // Appears to be an IPv6 address. Find the "]:" that separates the address from the port.
    auto pos = addr.rfind("]:");
    if (pos == std::string::npos) {
      return nullptr;
    }
    const auto ip_str = addr.substr(1, pos - 1);
    const auto port_str = addr.substr(pos + 2);
    uint64_t port64;
    if (port_str.empty() || !StringUtil::atoul(port_str.c_str(), port64, 10) || port64 > 65535) {
      return nullptr;
    }
    sockaddr_in6 sa6;
    if (ip_str.empty() || inet_pton(AF_INET6, ip_str.c_str(), &sa6.sin6_addr) != 1) {
      return nullptr;
    }
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = htons(port64);
    return InstanceConstSharedPtr(new Ipv6Instance(sa6));
  }
  // Treat it as an IPv4 address followed by a port.
  auto pos = addr.rfind(":");
  if (pos == std::string::npos) {
    return nullptr;
  }
  const auto ip_str = addr.substr(0, pos);
  const auto port_str = addr.substr(pos + 1);
  uint64_t port64;
  if (port_str.empty() || !StringUtil::atoul(port_str.c_str(), port64, 10) || port64 > 65535) {
    return nullptr;
  }
  sockaddr_in sa4;
  if (ip_str.empty() || inet_pton(AF_INET, ip_str.c_str(), &sa4.sin_addr) != 1) {
    return nullptr;
  }
  sa4.sin_family = AF_INET;
  sa4.sin_port = htons(port64);
  return InstanceConstSharedPtr(new Ipv4Instance(&sa4));
}

} // Address
} // Network
