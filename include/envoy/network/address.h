#pragma once

#include <sys/types.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/api/os_sys_calls.h"
#include "envoy/common/platform.h"
#include "envoy/common/pure.h"

#include "absl/numeric/int128.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Network {
namespace Address {

/**
 * Interface for an Ipv4 address.
 */
class Ipv4 {
public:
  virtual ~Ipv4() = default;

  /**
   * @return the 32-bit IPv4 address in network byte order.
   */
  virtual uint32_t address() const PURE;
};

/**
 * Interface for an Ipv6 address.
 */
class Ipv6 {
public:
  virtual ~Ipv6() = default;

  /**
   * @return the absl::uint128 IPv6 address in network byte order.
   */
  virtual absl::uint128 address() const PURE;

  /**
   * @return true if address is Ipv6 and Ipv4 compatibility is disabled, false otherwise
   */
  virtual bool v6only() const PURE;
};

enum class IpVersion { v4, v6 }; // NOLINT(readability-identifier-naming)

/**
 * Interface for a generic IP address.
 */
class Ip {
public:
  virtual ~Ip() = default;

  /**
   * @return the address as a string. E.g., "1.2.3.4" for an IPv4 address.
   */
  virtual const std::string& addressAsString() const PURE;

  /**
   * @return whether this address is wild card, i.e., '0.0.0.0'.
   */
  virtual bool isAnyAddress() const PURE;

  /**
   * @return whether this address is a valid unicast address, i.e., not an wild card, broadcast, or
   * multicast address.
   */
  virtual bool isUnicastAddress() const PURE;

  /**
   * @return Ipv4 address data IFF version() == IpVersion::v4, otherwise nullptr.
   */
  virtual const Ipv4* ipv4() const PURE;

  /**
   * @return Ipv6 address data IFF version() == IpVersion::v6, otherwise nullptr.
   */
  virtual const Ipv6* ipv6() const PURE;

  /**
   * @return the port associated with the address. Port may be zero if not specified, not
   * determinable before socket creation, or not applicable.
   * The port is in host byte order.
   */
  virtual uint32_t port() const PURE;

  /**
   * @return the version of IP address.
   */
  virtual IpVersion version() const PURE;
};

/**
 * Interface for a generic Pipe address
 */
class Pipe {
public:
  virtual ~Pipe() = default;
  /**
   * @return abstract namespace flag
   */
  virtual bool abstractNamespace() const PURE;

  /**
   * @return pipe mode
   */
  virtual mode_t mode() const PURE;
};

enum class Type { Ip, Pipe };

/**
 * Interface for all network addresses.
 */
class Instance {
public:
  virtual ~Instance() = default;

  virtual bool operator==(const Instance& rhs) const PURE;
  bool operator!=(const Instance& rhs) const { return !operator==(rhs); }

  /**
   * @return a human readable string for the address that represents the
   * physical/resolved address. (This will not necessarily include port
   * information, if applicable, since that may not be resolved until bind()).
   *
   * This string will be compatible with the following example formats:
   * For IPv4 addresses: "1.2.3.4:80"
   * For IPv6 addresses: "[1234:5678::9]:443"
   * For pipe addresses: "/foo"
   */
  virtual const std::string& asString() const PURE;

  /**
   * @return Similar to asString but returns a string view.
   */
  virtual absl::string_view asStringView() const PURE;

  /**
   * @return a human readable string for the address that represents the
   * logical/unresolved name.
   *
   * This string has a source-dependent format and should preserve the original
   * name for Address::Instances resolved by a Network::Address::Resolver.
   */
  virtual const std::string& logicalName() const PURE;

  /**
   * @return the IP address information IFF type() == Type::Ip, otherwise nullptr.
   */
  virtual const Ip* ip() const PURE;

  /**
   * @return the pipe address information IFF type() == Type::Pipe, otherwise nullptr.
   */
  virtual const Pipe* pipe() const PURE;

  /**
   * @return the underlying structure wherein the address is stored
   */
  virtual const sockaddr* sockAddr() const PURE;

  /**
   * @return length of the address container
   */
  virtual socklen_t sockAddrLen() const PURE;

  /**
   * @return the type of address.
   */
  virtual Type type() const PURE;
};

using InstanceConstSharedPtr = std::shared_ptr<const Instance>;

} // namespace Address
} // namespace Network
} // namespace Envoy
