#include "common/network/address_impl.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <array>
#include <cstdint>
#include <string>

#include "envoy/common/exception.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/network/io_socket_handle_impl.h"

namespace Envoy {
namespace Network {
namespace Address {

namespace {

// Validate that IPv4 is supported on this platform, raise an exception for the
// given address if not.
void validateIpv4Supported(const std::string& address) {
  static const bool supported = Network::Address::ipFamilySupported(AF_INET);
  if (!supported) {
    throw EnvoyException(
        fmt::format("IPv4 addresses are not supported on this machine: {}", address));
  }
}

// Validate that IPv6 is supported on this platform, raise an exception for the
// given address if not.
void validateIpv6Supported(const std::string& address) {
  static const bool supported = Network::Address::ipFamilySupported(AF_INET6);
  if (!supported) {
    throw EnvoyException(
        fmt::format("IPv6 addresses are not supported on this machine: {}", address));
  }
}

// Constructs a readable string with the embedded nulls in the abstract path replaced with '@'.
std::string friendlyNameFromAbstractPath(absl::string_view path) {
  std::string friendly_name(path.data(), path.size());
  std::replace(friendly_name.begin(), friendly_name.end(), '\0', '@');
  return friendly_name;
}

} // namespace

// Check if an IP family is supported on this machine.
bool ipFamilySupported(int domain) {
  Api::OsSysCalls& os_sys_calls = Api::OsSysCallsSingleton::get();
  const Api::SysCallIntResult result = os_sys_calls.socket(domain, SOCK_STREAM, 0);
  if (result.rc_ >= 0) {
    RELEASE_ASSERT(os_sys_calls.close(result.rc_).rc_ == 0,
                   absl::StrCat("Fail to close fd: response code ", result.rc_));
  }
  return result.rc_ != -1;
}

Address::InstanceConstSharedPtr addressFromSockAddr(const sockaddr_storage& ss, socklen_t ss_len,
                                                    bool v6only) {
  RELEASE_ASSERT(ss_len == 0 || static_cast<unsigned int>(ss_len) >= sizeof(sa_family_t), "");
  switch (ss.ss_family) {
  case AF_INET: {
    RELEASE_ASSERT(ss_len == 0 || static_cast<unsigned int>(ss_len) == sizeof(sockaddr_in), "");
    const struct sockaddr_in* sin = reinterpret_cast<const struct sockaddr_in*>(&ss);
    ASSERT(AF_INET == sin->sin_family);
    return std::make_shared<Address::Ipv4Instance>(sin);
  }
  case AF_INET6: {
    RELEASE_ASSERT(ss_len == 0 || static_cast<unsigned int>(ss_len) == sizeof(sockaddr_in6), "");
    const struct sockaddr_in6* sin6 = reinterpret_cast<const struct sockaddr_in6*>(&ss);
    ASSERT(AF_INET6 == sin6->sin6_family);
    if (!v6only && IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
#if defined(__APPLE__)
      struct sockaddr_in sin = {
          {}, AF_INET, sin6->sin6_port, {sin6->sin6_addr.__u6_addr.__u6_addr32[3]}, {}};
#else
      struct sockaddr_in sin = {AF_INET, sin6->sin6_port, {sin6->sin6_addr.s6_addr32[3]}, {}};
#endif
      return std::make_shared<Address::Ipv4Instance>(&sin);
    } else {
      return std::make_shared<Address::Ipv6Instance>(*sin6, v6only);
    }
  }
  case AF_UNIX: {
    const struct sockaddr_un* sun = reinterpret_cast<const struct sockaddr_un*>(&ss);
    ASSERT(AF_UNIX == sun->sun_family);
    RELEASE_ASSERT(ss_len == 0 || static_cast<unsigned int>(ss_len) >=
                                      offsetof(struct sockaddr_un, sun_path) + 1,
                   "");
    return std::make_shared<Address::PipeInstance>(sun, ss_len);
  }
  default:
    throw EnvoyException(fmt::format("Unexpected sockaddr family: {}", ss.ss_family));
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
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
    RELEASE_ASSERT(::getsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &socket_v6only, &size_int) == 0, "");
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

IoHandlePtr InstanceBase::socketFromSocketType(SocketType socket_type) const {
#if defined(__APPLE__)
  int flags = 0;
#else
  int flags = SOCK_NONBLOCK;
#endif

  if (socket_type == SocketType::Stream) {
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

  const Api::SysCallIntResult result = Api::OsSysCallsSingleton::get().socket(domain, flags, 0);
  RELEASE_ASSERT(result.rc_ != -1,
                 fmt::format("socket(2) failed, got error: {}", strerror(result.errno_)));
  IoHandlePtr io_handle = std::make_unique<IoSocketHandleImpl>(result.rc_);

#ifdef __APPLE__
  // Cannot set SOCK_NONBLOCK as a ::socket flag.
  RELEASE_ASSERT(fcntl(io_handle->fd(), F_SETFL, O_NONBLOCK) != -1, "");
#endif

  return io_handle;
}

Ipv4Instance::Ipv4Instance(const sockaddr_in* address) : InstanceBase(Type::Ip) {
  ip_.ipv4_.address_ = *address;
  ip_.friendly_address_ = sockaddrToString(*address);

  // Based on benchmark testing, this reserve+append implementation runs faster than absl::StrCat.
  fmt::format_int port(ntohs(address->sin_port));
  friendly_name_.reserve(ip_.friendly_address_.size() + 1 + port.size());
  friendly_name_.append(ip_.friendly_address_);
  friendly_name_.push_back(':');
  friendly_name_.append(port.data(), port.size());
  validateIpv4Supported(friendly_name_);
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
  validateIpv4Supported(friendly_name_);
  ip_.friendly_address_ = address;
}

Ipv4Instance::Ipv4Instance(uint32_t port) : InstanceBase(Type::Ip) {
  memset(&ip_.ipv4_.address_, 0, sizeof(ip_.ipv4_.address_));
  ip_.ipv4_.address_.sin_family = AF_INET;
  ip_.ipv4_.address_.sin_port = htons(port);
  ip_.ipv4_.address_.sin_addr.s_addr = INADDR_ANY;
  friendly_name_ = fmt::format("0.0.0.0:{}", port);
  validateIpv4Supported(friendly_name_);
  ip_.friendly_address_ = "0.0.0.0";
}

bool Ipv4Instance::operator==(const Instance& rhs) const {
  const Ipv4Instance* rhs_casted = dynamic_cast<const Ipv4Instance*>(&rhs);
  return (rhs_casted && (ip_.ipv4_.address() == rhs_casted->ip_.ipv4_.address()) &&
          (ip_.port() == rhs_casted->ip_.port()));
}

Api::SysCallIntResult Ipv4Instance::bind(int fd) const {
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  return os_syscalls.bind(fd, sockAddr(), sockAddrLen());
}

Api::SysCallIntResult Ipv4Instance::connect(int fd) const {
  const int rc = ::connect(fd, sockAddr(), sockAddrLen());
  return {rc, errno};
}

IoHandlePtr Ipv4Instance::socket(SocketType type) const { return socketFromSocketType(type); }

std::string Ipv4Instance::sockaddrToString(const sockaddr_in& addr) {
  static constexpr size_t BufferSize = 16; // enough space to hold an IPv4 address in string form
  char str[BufferSize];
  // Write backwards from the end of the buffer for simplicity.
  char* start = str + BufferSize;
  uint32_t ipv4_addr = ntohl(addr.sin_addr.s_addr);
  for (unsigned i = 4; i != 0; i--, ipv4_addr >>= 8) {
    uint32_t octet = ipv4_addr & 0xff;
    if (octet == 0) {
      ASSERT(start > str);
      *--start = '0';
    } else {
      do {
        ASSERT(start > str);
        *--start = '0' + (octet % 10);
        octet /= 10;
      } while (octet != 0);
    }
    if (i != 1) {
      ASSERT(start > str);
      *--start = '.';
    }
  }
  return std::string(start, str + BufferSize - start);
}

absl::uint128 Ipv6Instance::Ipv6Helper::address() const {
  absl::uint128 result{0};
  static_assert(sizeof(absl::uint128) == 16, "The size of asbl::uint128 is not 16.");
  memcpy(static_cast<void*>(&result), static_cast<const void*>(&address_.sin6_addr.s6_addr),
         sizeof(absl::uint128));
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
  validateIpv6Supported(friendly_name_);
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
  validateIpv6Supported(friendly_name_);
}

Ipv6Instance::Ipv6Instance(uint32_t port) : Ipv6Instance("", port) {}

bool Ipv6Instance::operator==(const Instance& rhs) const {
  const auto* rhs_casted = dynamic_cast<const Ipv6Instance*>(&rhs);
  return (rhs_casted && (ip_.ipv6_.address() == rhs_casted->ip_.ipv6_.address()) &&
          (ip_.port() == rhs_casted->ip_.port()));
}

Api::SysCallIntResult Ipv6Instance::bind(int fd) const {
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  return os_syscalls.bind(fd, sockAddr(), sockAddrLen());
}

Api::SysCallIntResult Ipv6Instance::connect(int fd) const {
  const int rc = ::connect(fd, sockAddr(), sockAddrLen());
  return {rc, errno};
}

IoHandlePtr Ipv6Instance::socket(SocketType type) const {
  IoHandlePtr io_handle = socketFromSocketType(type);
  // Setting IPV6_V6ONLY restricts the IPv6 socket to IPv6 connections only.
  const int v6only = ip_.v6only_;
  RELEASE_ASSERT(
      ::setsockopt(io_handle->fd(), IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)) != -1, "");
  return io_handle;
}

PipeInstance::PipeInstance(const sockaddr_un* address, socklen_t ss_len)
    : InstanceBase(Type::Pipe) {
  if (address->sun_path[0] == '\0') {
#if !defined(__linux__)
    throw EnvoyException("Abstract AF_UNIX sockets are only supported on linux.");
#endif
    RELEASE_ASSERT(static_cast<unsigned int>(ss_len) >= offsetof(struct sockaddr_un, sun_path) + 1,
                   "");
    abstract_namespace_ = true;
    address_length_ = ss_len - offsetof(struct sockaddr_un, sun_path);
  }
  address_ = *address;
  if (abstract_namespace_) {
    // Replace all null characters with '@' in friendly_name_.
    friendly_name_ =
        friendlyNameFromAbstractPath(absl::string_view(address_.sun_path, address_length_));
  } else {
    friendly_name_ = address->sun_path;
  }
}

PipeInstance::PipeInstance(const std::string& pipe_path) : InstanceBase(Type::Pipe) {
  if (pipe_path.size() >= sizeof(address_.sun_path)) {
    throw EnvoyException(
        fmt::format("Path \"{}\" exceeds maximum UNIX domain socket path size of {}.", pipe_path,
                    sizeof(address_.sun_path)));
  }
  memset(&address_, 0, sizeof(address_));
  address_.sun_family = AF_UNIX;
  if (pipe_path[0] == '@') {
    // This indicates an abstract namespace.
    // In this case, null bytes in the name have no special significance, and so we copy all
    // characters of pipe_path to sun_path, including null bytes in the name. The pathname must also
    // be null terminated. The friendly name is the address path with embedded nulls replaced with
    // '@' for consistency with the first character.
#if !defined(__linux__)
    throw EnvoyException("Abstract AF_UNIX sockets are only supported on linux.");
#endif
    abstract_namespace_ = true;
    address_length_ = pipe_path.size();
    memcpy(&address_.sun_path[0], pipe_path.data(), pipe_path.size());
    address_.sun_path[0] = '\0';
    address_.sun_path[pipe_path.size()] = '\0';
    friendly_name_ =
        friendlyNameFromAbstractPath(absl::string_view(address_.sun_path, address_length_));
  } else {
    // Throw an error if the pipe path has an embedded null character.
    if (pipe_path.size() != strlen(pipe_path.c_str())) {
      throw EnvoyException("UNIX domain socket pathname contains embedded null characters");
    }
    StringUtil::strlcpy(&address_.sun_path[0], pipe_path.c_str(), sizeof(address_.sun_path));
    friendly_name_ = address_.sun_path;
  }
}

bool PipeInstance::operator==(const Instance& rhs) const { return asString() == rhs.asString(); }

Api::SysCallIntResult PipeInstance::bind(int fd) const {
  if (!abstract_namespace_) {
    // Try to unlink an existing filesystem object at the requested path. Ignore
    // errors -- it's fine if the path doesn't exist, and if it exists but can't
    // be unlinked then `::bind()` will generate a reasonable errno.
    unlink(address_.sun_path);
  }
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  return os_syscalls.bind(fd, sockAddr(), sockAddrLen());
}

Api::SysCallIntResult PipeInstance::connect(int fd) const {
  const int rc = ::connect(fd, sockAddr(), sockAddrLen());
  return {rc, errno};
}

IoHandlePtr PipeInstance::socket(SocketType type) const { return socketFromSocketType(type); }

} // namespace Address
} // namespace Network
} // namespace Envoy
