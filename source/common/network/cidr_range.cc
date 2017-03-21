#include "cidr_range.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

namespace Network {
namespace Address {

CidrRange::CidrRange() : length_(-1) {}

CidrRange::CidrRange(InstancePtr address, int length)
    : address_(std::move(address)), length_(length) {
  // This is a private ctor, so only checking these asserts in debug builds.
  if (address_ == nullptr) {
    ASSERT(length_ == -1);
  } else {
    ASSERT(address_->type() == Type::Ip);
    ASSERT(length_ >= 0);
  }
}

CidrRange::CidrRange(const CidrRange& other) : address_(other.address_), length_(other.length_) {}

CidrRange& CidrRange::operator=(const CidrRange& other) {
  address_ = other.address_;
  length_ = other.length_;
  return *this;
}

bool CidrRange::operator==(const CidrRange& other) const {
  // Lengths must be the same, and must be valid (i.e. not -1).
  if (length_ != other.length_ || length_ == -1) {
    return false;
  }
  if (version() == IpVersion::v4) {
    return other.version() == IpVersion::v4 && ipv4()->address() == other.ipv4()->address();
  } else {
    return other.version() == IpVersion::v6 && ipv6()->address() == other.ipv6()->address();
  }
}

const Ipv4* CidrRange::ipv4() const {
  if (address_ != nullptr) {
    return address_->ip()->ipv4();
  }
  return nullptr;
}

const Ipv6* CidrRange::ipv6() const {
  if (address_ != nullptr) {
    return address_->ip()->ipv6();
  }
  return nullptr;
}

int CidrRange::length() const { return length_; }

IpVersion CidrRange::version() const {
  if (address_ != nullptr) {
    return address_->ip()->version();
  }
  return IpVersion::v4;
}

bool CidrRange::isInRange(InstancePtr address) const {
  if (address == nullptr || length_ < 0 || address->type() != Type::Ip ||
      address->ip()->version() != version()) {
    return false;
  }
  // Make an CidrRange from the address, of the same length as this. If the two ranges have
  // are the same, then the address is in this range.
  CidrRange other = create(address, length_);
  ASSERT(length() == other.length());
  ASSERT(version() == other.version());
  return *this == other;
}

std::string CidrRange::asString() const {
  if (address_ == nullptr) {
    return "/-1";
  } else {
    return fmt::format("{}/{}", address_->ip()->addressAsString(), length_);
  }
}

// static
CidrRange CidrRange::create(InstancePtr address, int length) {
  InstancePtr ptr = truncateIpAddressAndLength(std::move(address), &length);
  return CidrRange(std::move(ptr), length);
}

// static
CidrRange CidrRange::create(const std::string& address, int length) {
  return create(parseInternetAddress(address), length);
}

// static
CidrRange CidrRange::create(const std::string& range) {
  std::vector<std::string> parts = StringUtil::split(range, '/');
  if (parts.size() == 2) {
    InstancePtr ptr = parseInternetAddress(parts[0]);
    if (ptr != nullptr && ptr->type() == Type::Ip) {
      uint64_t length64;
      if (StringUtil::atoul(parts[1].c_str(), length64, 10)) {
        if ((ptr->ip()->version() == IpVersion::v6 && length64 <= 128) ||
            (ptr->ip()->version() == IpVersion::v4 && length64 <= 32)) {
          return create(std::move(ptr), static_cast<uint32_t>(length64));
        }
      }
    }
  }
  return CidrRange(nullptr, -1);
}

// static
InstancePtr CidrRange::truncateIpAddressAndLength(InstancePtr address, int* length_io) {
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
  NOT_REACHED
}

} // Address
} // Network
