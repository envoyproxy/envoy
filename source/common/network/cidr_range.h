#pragma once

#include <string>
#include <vector>

#include "envoy/json/json_object.h"
#include "envoy/network/address.h"

#include "common/protobuf/protobuf.h"

#include "api/address.pb.h"

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

  /**
   * Copies an existing CidrRange.
   */
  CidrRange(const CidrRange& other);

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
   * TODO(jamessynge) Consider making this Optional<int> length, or modifying the create() methods
   *                  below to return Optional<CidrRange> (the latter is probably better).
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
   * @return true if this instance is valid; address != nullptr && length is appropriate for the
   *         IP version (these are checked during construction, and reduced down to a check of
   *         the length).
   */
  bool isValid() const { return length_ >= 0; }

  /**
   * @return a CidrRange instance with the specified address and length, modified so that the only
   *         bits that might be non-zero are in the high-order length bits, and so that length is
   *         in the appropriate range (0 to 32 for IPv4, 0 to 128 for IPv6). If the the address or
   *         length is invalid, then the range will be invalid (i.e. length == -1).
   */
  static CidrRange create(InstanceConstSharedPtr address, int length);
  static CidrRange create(const std::string& address, int length);

  /**
   * Constructs an CidrRange from a string with this format (same as returned
   * by CidrRange::asString above):
   *      <address>/<length>    e.g. "10.240.0.0/16" or "1234:5678::/64"
   * @return a CidrRange instance with the specified address and length if parsed successfully,
   *         else with no address and a length of -1.
   */
  static CidrRange create(const std::string& range);

  /**
   * Constructs a CidrRange from envoy::api::v2::CidrRange.
   */
  static CidrRange create(const envoy::api::v2::CidrRange& cidr);

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
  IpList(const std::vector<std::string>& subnets);
  IpList(const Json::Object& config, const std::string& member_name);
  IpList(const Protobuf::RepeatedPtrField<envoy::api::v2::CidrRange>& cidrs);
  IpList(){};

  bool contains(const Instance& address) const;
  bool empty() const { return ip_list_.empty(); }

private:
  std::vector<CidrRange> ip_list_;
};

} // namespace Address
} // namespace Network
} // namespace Envoy
