#include "common/network/address_impl.h"

#include <array>
#include <cstdint>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/network/socket_interface.h"

namespace Envoy {
namespace Network {
namespace Address {

namespace {

// Validate that IPv4 is supported on this platform, raise an exception for the
// given address if not.
void validateIpv4Supported(const std::string& address) {
  static const bool supported = SocketInterfaceSingleton::get().ipFamilySupported(AF_INET);
  if (!supported) {
    throw EnvoyException(
        fmt::format("IPv4 addresses are not supported on this machine: {}", address));
  }
}

// Validate that IPv6 is supported on this platform, raise an exception for the
// given address if not.
void validateIpv6Supported(const std::string& address) {
  static const bool supported = SocketInterfaceSingleton::get().ipFamilySupported(AF_INET6);
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
#elif defined(WIN32)
      struct in_addr in_v4 = {};
      in_v4.S_un.S_addr = reinterpret_cast<const uint32_t*>(sin6->sin6_addr.u.Byte)[3];
      struct sockaddr_in sin = {AF_INET, sin6->sin6_port, in_v4, {}};
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

  friendly_name_ = absl::StrCat(address, ":", port);
  validateIpv4Supported(friendly_name_);
  ip_.friendly_address_ = address;
}

Ipv4Instance::Ipv4Instance(uint32_t port) : InstanceBase(Type::Ip) {
  memset(&ip_.ipv4_.address_, 0, sizeof(ip_.ipv4_.address_));
  ip_.ipv4_.address_.sin_family = AF_INET;
  ip_.ipv4_.address_.sin_port = htons(port);
  ip_.ipv4_.address_.sin_addr.s_addr = INADDR_ANY;
  friendly_name_ = absl::StrCat("0.0.0.0:", port);
  validateIpv4Supported(friendly_name_);
  ip_.friendly_address_ = "0.0.0.0";
}

bool Ipv4Instance::operator==(const Instance& rhs) const {
  const Ipv4Instance* rhs_casted = dynamic_cast<const Ipv4Instance*>(&rhs);
  return (rhs_casted && (ip_.ipv4_.address() == rhs_casted->ip_.ipv4_.address()) &&
          (ip_.port() == rhs_casted->ip_.port()));
}

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

bool Ipv6Instance::Ipv6Helper::v6only() const { return v6only_; };

std::string Ipv6Instance::Ipv6Helper::makeFriendlyAddress() const {
  char str[INET6_ADDRSTRLEN];
  const char* ptr = inet_ntop(AF_INET6, &address_.sin6_addr, str, INET6_ADDRSTRLEN);
  ASSERT(str == ptr);
  return ptr;
}

Ipv6Instance::Ipv6Instance(const sockaddr_in6& address, bool v6only) : InstanceBase(Type::Ip) {
  ip_.ipv6_.address_ = address;
  ip_.friendly_address_ = ip_.ipv6_.makeFriendlyAddress();
  ip_.ipv6_.v6only_ = v6only;
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

PipeInstance::PipeInstance(const sockaddr_un* address, socklen_t ss_len, mode_t mode)
    : InstanceBase(Type::Pipe) {
  if (address->sun_path[0] == '\0') {
#if !defined(__linux__)
    throw EnvoyException("Abstract AF_UNIX sockets are only supported on linux.");
#endif
    RELEASE_ASSERT(static_cast<unsigned int>(ss_len) >= offsetof(struct sockaddr_un, sun_path) + 1,
                   "");
    pipe_.abstract_namespace_ = true;
    pipe_.address_length_ = ss_len - offsetof(struct sockaddr_un, sun_path);
  }
  pipe_.address_ = *address;
  if (pipe_.abstract_namespace_) {
    if (mode != 0) {
      throw EnvoyException("Cannot set mode for Abstract AF_UNIX sockets");
    }
    // Replace all null characters with '@' in friendly_name_.
    friendly_name_ = friendlyNameFromAbstractPath(
        absl::string_view(pipe_.address_.sun_path, pipe_.address_length_));
  } else {
    friendly_name_ = address->sun_path;
  }
  pipe_.mode_ = mode;
}

PipeInstance::PipeInstance(const std::string& pipe_path, mode_t mode) : InstanceBase(Type::Pipe) {
  if (pipe_path.size() >= sizeof(pipe_.address_.sun_path)) {
    throw EnvoyException(
        fmt::format("Path \"{}\" exceeds maximum UNIX domain socket path size of {}.", pipe_path,
                    sizeof(pipe_.address_.sun_path)));
  }
  memset(&pipe_.address_, 0, sizeof(pipe_.address_));
  pipe_.address_.sun_family = AF_UNIX;
  if (pipe_path[0] == '@') {
    // This indicates an abstract namespace.
    // In this case, null bytes in the name have no special significance, and so we copy all
    // characters of pipe_path to sun_path, including null bytes in the name. The pathname must also
    // be null terminated. The friendly name is the address path with embedded nulls replaced with
    // '@' for consistency with the first character.
#if !defined(__linux__)
    throw EnvoyException("Abstract AF_UNIX sockets are only supported on linux.");
#endif
    if (mode != 0) {
      throw EnvoyException("Cannot set mode for Abstract AF_UNIX sockets");
    }
    pipe_.abstract_namespace_ = true;
    pipe_.address_length_ = pipe_path.size();
    memcpy(&pipe_.address_.sun_path[0], pipe_path.data(), pipe_path.size());
    pipe_.address_.sun_path[0] = '\0';
    pipe_.address_.sun_path[pipe_path.size()] = '\0';
    friendly_name_ = friendlyNameFromAbstractPath(
        absl::string_view(pipe_.address_.sun_path, pipe_.address_length_));
  } else {
    // Throw an error if the pipe path has an embedded null character.
    if (pipe_path.size() != strlen(pipe_path.c_str())) {
      throw EnvoyException("UNIX domain socket pathname contains embedded null characters");
    }
    StringUtil::strlcpy(&pipe_.address_.sun_path[0], pipe_path.c_str(),
                        sizeof(pipe_.address_.sun_path));
    friendly_name_ = pipe_.address_.sun_path;
  }
  pipe_.mode_ = mode;
}

bool PipeInstance::operator==(const Instance& rhs) const { return asString() == rhs.asString(); }

} // namespace Address
} // namespace Network
} // namespace Envoy
