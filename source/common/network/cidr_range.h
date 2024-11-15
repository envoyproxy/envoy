#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/network/address.h"

#include "source/common/protobuf/protobuf.h"

#include "xds/core/v3/cidr.pb.h"

namespace Envoy {
namespace Network {
namespace Address {

/**
 * A "Classless Inter-Domain Routing" range of internet addresses, aka a CIDR range, consisting
 * of an Ip address and a count of leading bits included in the mask. Other than those leading
 * bits, all of the other bits of the Ip address are zero. For more info, see RFC1519 or
 * https://en.wikipedia.org/wiki/Classless_Inter-Domain_Routing.
 */
class CidrRange {
public:
  /**
   * Constructs an uninitialized range: length == -1, and there is no associated address.
   */
  CidrRange();

  CidrRange(const CidrRange& other) = default;
  CidrRange(CidrRange&& other) = default;

  /**
   * Overwrites this with other.
   */
  CidrRange& operator=(const CidrRange& other);

  /**
   * @return true if the ranges are identical.
   */
  bool operator==(const CidrRange& other) const;

  /**
   * @return Ip address data IFF length >= 0, otherwise nullptr.
   */
  const Ip* ip() const;

  /**
   * TODO(jamessynge) Consider making this absl::optional<int> length, or modifying the create()
   * methods below to return absl::optional<CidrRange> (the latter is probably better).
   * @return the number of bits of the address that are included in the mask. -1 if uninitialized
   *         or invalid, else in the range 0 to 32 for IPv4, and 0 to 128 for IPv6.
   */
  int length() const;

  /**
   * @return true if the address argument is in the range of this object, false if not, including
             if the range is uninitialized or if the argument is not of the same IpVersion.
   */
  bool isInRange(const Instance& address) const;

  /**
   * @return a human readable string for the range. This string will be in the following format:
   *         - For IPv4 ranges: "1.2.3.4/32" or "10.240.0.0/16"
   *         - For IPv6 ranges: "1234:5678::f/128" or "1234:5678::/64"
   */
  std::string asString() const;

  /**
   * @return a CidrRange instance with the specified address and length, modified so that the only
   *         bits that might be non-zero are in the high-order length bits, and so that length is
   *         in the appropriate range (0 to 32 for IPv4, 0 to 128 for IPv6). If the address or
   *         length is invalid, then the range will be invalid (i.e. length == -1).
   */
  static absl::StatusOr<CidrRange>
  create(InstanceConstSharedPtr address, int length,
         absl::optional<absl::string_view> original_address_str = "");
  static absl::StatusOr<CidrRange> create(const std::string& address, int length);

  /**
   * Constructs an CidrRange from a string with this format (same as returned
   * by CidrRange::asString above):
   *      <address>/<length>    e.g. "10.240.0.0/16" or "1234:5678::/64"
   * @return a CidrRange instance with the specified address and length if parsed successfully,
   *         else with no address and a length of -1.
   */
  static absl::StatusOr<CidrRange> create(const std::string& range);

  /**
   * Constructs a CidrRange from envoy::config::core::v3::CidrRange.
   */
  static absl::StatusOr<CidrRange> create(const envoy::config::core::v3::CidrRange& cidr);

  /**
   * Constructs a CidrRange from xds::core::v3::CidrRange.
   */
  static absl::StatusOr<CidrRange> create(const xds::core::v3::CidrRange& cidr);

  /**
   * Given an IP address and a length of high order bits to keep, returns an address
   * where those high order bits are unmodified, and the remaining bits are all zero.
   * length_io is reduced to be at most 32 for IPv4 address and at most 128 for IPv6
   * addresses. If the address is invalid or the length is less than zero, then *length_io
   * is set to -1 and nullptr is returned.
   * @return a pointer to an address where the high order *length_io bits are unmodified
   *         from address, and *length_io is in the range 0 to N, where N is the number of bits
   *         in an address of the IP version (i.e. address->ip()->version()).
   */
  static InstanceConstSharedPtr truncateIpAddressAndLength(InstanceConstSharedPtr address,
                                                           int* length_io);

private:
  /**
   * @return true if this instance is valid; address != nullptr && length is appropriate for the
   *         IP version (these are checked during construction, and reduced down to a check of
   *         the length).
   */
  bool isValid() const { return length_ >= 0; }

  CidrRange(InstanceConstSharedPtr address, int length);

  InstanceConstSharedPtr address_;
  int length_;
};

/**
 * Class for keeping a list of CidrRanges, and then determining whether an
 * IP address is in the CidrRange list.
 */
class IpList {
public:
  static absl::StatusOr<std::unique_ptr<IpList>>
  create(const Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange>& cidrs);

  IpList() = default;

  bool contains(const Instance& address) const;
  size_t getIpListSize() const { return ip_list_.size(); };
  const std::string& error() const { return error_; }

private:
  explicit IpList(const Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange>& cidrs);
  std::vector<CidrRange> ip_list_;
  std::string error_;
};

} // namespace Address
} // namespace Network
} // namespace Envoy
