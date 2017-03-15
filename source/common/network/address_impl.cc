#include "address_impl.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/utility.h"

namespace Network {
namespace Address {

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
  ip_.ipv6_.address_.sin6_family = AF_INET;
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
  return ::socket(AF_INET, flagsFromSocketType(type), 0);
}

PipeInstance::PipeInstance(const sockaddr_un* address) : InstanceBase(Type::Pipe) {
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

InstancePtr parseInternetAddress(const std::string& ipAddr) {
  sockaddr_in sa4;
  if (inet_pton(AF_INET, ipAddr.c_str(), &sa4.sin_addr) == 1) {
    sa4.sin_family = AF_INET;
    sa4.sin_port = 0;
    return InstancePtr(new Ipv4Instance(&sa4));
  }
  sockaddr_in6 sa6;
  if (inet_pton(AF_INET6, ipAddr.c_str(), &sa6.sin6_addr) == 1) {
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = 0;
    return InstancePtr(new Ipv6Instance(sa6));
  }
  return nullptr;
}

IpRange::IpRange() : length_(-1) {}

IpRange::IpRange(const InstancePtr& address, int length) : address_(address), length_(length) {
  // This is a private ctor, so only checking these asserts in debug builds.
  if (address_ == nullptr) {
    ASSERT(length_ == -1);
  } else {
    ASSERT(address_->type() == Type::Ip);
    ASSERT(length_ >= 0);
  }
}

IpRange::IpRange(const IpRange& other) : address_(other.address_), length_(other.length_) {}

IpRange& IpRange::operator=(const IpRange& other) {
  address_ = other.address_;
  length_ = other.length_;
  return *this;
}

const Ipv4* IpRange::ipv4() const {
  if (address_ != nullptr) {
    return address_->ip()->ipv4();
  }
  return nullptr;
}

const Ipv6* IpRange::ipv6() const {
  if (address_ != nullptr) {
    return address_->ip()->ipv6();
  }
  return nullptr;
}

int IpRange::length() const { return length_; }

IpVersion IpRange::version() const {
  if (address_ != nullptr) {
    return address_->ip()->version();
  }
  return IpVersion::v4;
}

bool IpRange::isInRange(const InstancePtr& address) const {
  if (address == nullptr || length_ < 0 || address->type() != Type::Ip ||
      address->ip()->version() != version()) {
    return false;
  }
  // Make an IpRange from the address, of the same length as this. If the two ranges have
  // the same address, then the address is in this range.
  IpRange other = construct(address, length_);
  ASSERT(length() == other.length());
  ASSERT(version() == other.version());
  if (version() == IpVersion::v4) {
    return ipv4()->address() == other.ipv4()->address();
  } else {
    return ipv6()->address() == other.ipv6()->address();
  }
}

std::string IpRange::asString() const {
  if (address_ == nullptr) {
    return "/-1";
  } else {
    return fmt::format("{}/{}", address_->ip()->addressAsString(), length_);
  }
}

InstancePtr truncateIpAddressAndLength(const InstancePtr& address, int* length_io) {
  int length = *length_io;
  if (address == nullptr || length < 0 || address->type() != Type::Ip) {
    *length_io = -1;
    return nullptr;
  }
  switch (address->ip()->version()) {
  case IpVersion::v4: {
    if (length >= 32) {
      // We're using all of the bits, so don't need to create a new address instance.
      *length_io = 32;
      return address;
    } else if (length == 0) {
      // Create an Ipv4Instance with only a port, which will thus have the any address.
      // TODO Consider adding an Any4() static method to return such an instance.
      return InstancePtr(new Ipv4Instance(uint32_t(0)));
    }
    // Need to mask out the unused bits, and create an Ipv4Instance with this address.
    uint32_t ip4 = ntohl(address->ip()->ipv4()->address());
    ip4 &= ~0U << (32 - length);
    sockaddr_in sa4;
    sa4.sin_family = AF_INET;
    sa4.sin_port = htons(0);
    sa4.sin_addr.s_addr = htonl(ip4);
    return InstancePtr(new Ipv4Instance(&sa4));
  }

  case IpVersion::v6: {
    if (length >= 128) {
      // We're using all of the bits, so don't need to create a new address instance.
      *length_io = 128;
      return address;
    } else if (length == 0) {
      // Create an Ipv6Instance with only a port, which will thus have the any address.
      // TODO Consider adding an Any6() static method to return such an instance.
      return InstancePtr(new Ipv6Instance(uint32_t(0)));
    }
    // We need to mask out the unused bits, but we don't have a uint128_t available.
    // However, we know that the array returned by Ipv6::address() represents the address
    // in network order (bit 7 of byte 0 is the high-order bit of the address), so we
    // simply need to keep the leading length bits of the array, and get rid of the rest.
    sockaddr_in6 sa6;
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = htons(0);
    std::array<uint8_t, 16> ip6 = address->ip()->ipv6()->address();
    for (int i = 0; i < 16; ++i) {
      if (length == 0) {
        // We've retained all the bits we need, the remaining are all zero.
        sa6.sin6_addr.s6_addr[i] = 0;
      } else if (length < 8) {
        // We're retaining only the high-order length bits of this byte.
        sa6.sin6_addr.s6_addr[i] = ip6[i] & (0xff << (8 - length));
        length = 0;
      } else {
        // We're keeping all of these bits.
        sa6.sin6_addr.s6_addr[i] = ip6[i];
        length -= 8;
      }
    }
    return InstancePtr(new Ipv6Instance(sa6));
  }
  }
  return nullptr; // UNREACHED
}

IpRange IpRange::construct(const InstancePtr& address, int length) {
  InstancePtr ptr = truncateIpAddressAndLength(address, &length);
  return IpRange(ptr, length);
}

IpRange IpRange::construct(const std::string& address, int length) {
  return construct(parseInternetAddress(address), length);
}

IpRange IpRange::construct(const std::string& range) {
  std::vector<std::string> parts = StringUtil::split(range, '/');
  if (parts.size() == 2) {
    InstancePtr ptr = parseInternetAddress(parts[0]);
    if (ptr != nullptr && ptr->type() == Type::Ip) {
      uint64_t length64;
      if (StringUtil::atoul(parts[1].c_str(), length64, 10)) {
        if ((ptr->ip()->version() == IpVersion::v6 && length64 <= 128) ||
            (ptr->ip()->version() == IpVersion::v4 && length64 <= 32)) {
          return construct(ptr, static_cast<uint32_t>(length64));
        }
      }
    }
  }
  return IpRange(nullptr, -1);
}

} // Address
} // Network
