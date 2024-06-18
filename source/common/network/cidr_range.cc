#include "source/common/network/cidr_range.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/safe_memcpy.h"
#include "source/common/common/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"

namespace Envoy {
namespace Network {
namespace Address {

CidrRange::CidrRange() : length_(-1) {}

CidrRange::CidrRange(InstanceConstSharedPtr address, int length)
    : address_(std::move(address)), length_(length) {
  // This is a private ctor, so only checking these asserts in debug builds.
  if (address_ == nullptr) {
    ASSERT(length_ == -1);
  } else {
    ASSERT(address_->type() == Type::Ip);
    ASSERT(length_ >= 0);
  }
}

CidrRange& CidrRange::operator=(const CidrRange& other) = default;

bool CidrRange::operator==(const CidrRange& other) const {
  // Lengths must be the same, and must be valid (i.e. not -1).
  if (length_ != other.length_ || length_ == -1) {
    return false;
  }

  if (address_ == nullptr || other.address_ == nullptr) {
    return false;
  }

  if (address_->ip()->version() == IpVersion::v4) {
    return other.address_->ip()->version() == IpVersion::v4 &&
           address_->ip()->ipv4()->address() == other.address_->ip()->ipv4()->address();
  } else {
    return other.address_->ip()->version() == IpVersion::v6 &&
           address_->ip()->ipv6()->address() == other.address_->ip()->ipv6()->address();
  }
}

const Ip* CidrRange::ip() const {
  if (address_ != nullptr) {
    return address_->ip();
  }
  return nullptr;
}

int CidrRange::length() const { return length_; }

bool CidrRange::isInRange(const Instance& address) const {
  if (address_ == nullptr || !isValid() || address.type() != Type::Ip ||
      address_->ip()->version() != address.ip()->version()) {
    return false;
  }

  // All addresses in range.
  if (length_ == 0) {
    return true;
  }

  switch (address.ip()->version()) {
  case IpVersion::v4:
    if (ntohl(address.ip()->ipv4()->address()) >> (32 - length_) ==
        ntohl(address_->ip()->ipv4()->address()) >> (32 - length_)) {
      return true;
    }
    break;
  case IpVersion::v6:
    if ((Utility::Ip6ntohl(address_->ip()->ipv6()->address()) >> (128 - length_)) ==
        (Utility::Ip6ntohl(address.ip()->ipv6()->address()) >> (128 - length_))) {
      return true;
    }
    break;
  }
  return false;
}

std::string CidrRange::asString() const {
  if (address_ == nullptr) {
    return "/-1";
  } else {
    return fmt::format("{}/{}", address_->ip()->addressAsString(), length_);
  }
}

// static
absl::StatusOr<CidrRange>
CidrRange::create(InstanceConstSharedPtr address, int length,
                  absl::optional<absl::string_view> original_address_str) {
  InstanceConstSharedPtr ptr = truncateIpAddressAndLength(std::move(address), &length);
  if (!ptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "malformed IP address: ", original_address_str.has_value() ? *original_address_str : ""));
  }
  CidrRange ret = CidrRange(std::move(ptr), length);
  if (ret.isValid()) {
    return ret;
  }
  return absl::InvalidArgumentError("Invalid CIDR range");
}

// static
absl::StatusOr<CidrRange> CidrRange::create(const std::string& address, int length) {
  return create(Utility::parseInternetAddressNoThrow(address), length, address);
}

absl::StatusOr<CidrRange> CidrRange::create(const envoy::config::core::v3::CidrRange& cidr) {
  return create(Utility::parseInternetAddressNoThrow(cidr.address_prefix()),
                cidr.prefix_len().value(), cidr.address_prefix());
}

absl::StatusOr<CidrRange> CidrRange::create(const xds::core::v3::CidrRange& cidr) {
  return create(Utility::parseInternetAddressNoThrow(cidr.address_prefix()),
                cidr.prefix_len().value(), cidr.address_prefix());
}

// static
absl::StatusOr<CidrRange> CidrRange::create(const std::string& range) {
  const auto parts = StringUtil::splitToken(range, "/");
  if (parts.size() == 2) {
    InstanceConstSharedPtr ptr = Utility::parseInternetAddressNoThrow(std::string{parts[0]});
    if (ptr && ptr->type() == Type::Ip) {
      uint64_t length64;
      if (absl::SimpleAtoi(parts[1], &length64)) {
        if ((ptr->ip()->version() == IpVersion::v6 && length64 <= 128) ||
            (ptr->ip()->version() == IpVersion::v4 && length64 <= 32)) {
          return create(std::move(ptr), static_cast<uint32_t>(length64));
        }
      }
    }
  }
  return absl::InvalidArgumentError("Invalid CIDR range");
}

// static
InstanceConstSharedPtr CidrRange::truncateIpAddressAndLength(InstanceConstSharedPtr address,
                                                             int* length_io) {
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
      return std::make_shared<Ipv4Instance>(uint32_t(0));
    }
    // Need to mask out the unused bits, and create an Ipv4Instance with this address.
    uint32_t ip4 = ntohl(address->ip()->ipv4()->address());
    ip4 &= ~0U << (32 - length);
    sockaddr_in sa4;
    memset(&sa4, 0, sizeof(sa4));
    sa4.sin_family = AF_INET;
    sa4.sin_port = htons(0);
    sa4.sin_addr.s_addr = htonl(ip4);
    return std::make_shared<Ipv4Instance>(&sa4);
  }

  case IpVersion::v6: {
    if (length >= 128) {
      // We're using all of the bits, so don't need to create a new address instance.
      *length_io = 128;
      return address;
    } else if (length == 0) {
      // Create an Ipv6Instance with only a port, which will thus have the any address.
      return std::make_shared<Ipv6Instance>(uint32_t(0));
    }
    sockaddr_in6 sa6;
    memset(&sa6, 0, sizeof(sa6));
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = htons(0);

    // The maximum number stored in absl::uint128 has every bit set to 1.
    absl::uint128 mask = absl::Uint128Max();
    // Shifting the value to the left sets all bits between 128-length and 128 to zero.
    mask <<= (128 - length);
    // This will mask out the unused bits of the address.
    absl::uint128 ip6 = Utility::Ip6ntohl(address->ip()->ipv6()->address()) & mask;

    absl::uint128 ip6_htonl = Utility::Ip6htonl(ip6);
    static_assert(sizeof(absl::uint128) == 16, "The size of asbl::uint128 is not 16.");
    safeMemcpy(&sa6.sin6_addr.s6_addr, &ip6_htonl);
    return std::make_shared<Ipv6Instance>(sa6);
  }
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

absl::StatusOr<std::unique_ptr<IpList>>
IpList::create(const Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange>& cidrs) {
  std::unique_ptr<IpList> ret = std::unique_ptr<IpList>(new IpList(cidrs));
  if (!ret->error_.empty()) {
    return absl::InvalidArgumentError(ret->error_);
  }
  return ret;
}

IpList::IpList(const Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange>& cidrs) {
  ip_list_.reserve(cidrs.size());
  for (const envoy::config::core::v3::CidrRange& entry : cidrs) {
    absl::StatusOr<CidrRange> range_or_error = CidrRange::create(entry);
    if (range_or_error.status().ok()) {
      ip_list_.push_back(std::move(range_or_error.value()));
    } else {
      error_ = fmt::format("invalid ip/mask combo '{}/{}' (format is <ip>/<# mask bits>)",
                           entry.address_prefix(), entry.prefix_len().value());
    }
  }
}

bool IpList::contains(const Instance& address) const {
  for (const CidrRange& entry : ip_list_) {
    if (entry.isInRange(address)) {
      return true;
    }
  }
  return false;
}

} // namespace Address
} // namespace Network
} // namespace Envoy
