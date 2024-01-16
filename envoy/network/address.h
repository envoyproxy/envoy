#pragma once

#include <sys/types.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/common/pure.h"

#include "absl/numeric/int128.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Network {

/* Forward declaration */
class SocketInterface;

namespace Address {

class Instance;
using InstanceConstSharedPtr = std::shared_ptr<const Instance>;

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
   * @return the uint32_t scope/zone identifier of the IPv6 address.
   */
  virtual uint32_t scopeId() const PURE;

  /**
   * @return true if address is Ipv6 and Ipv4 compatibility is disabled, false otherwise
   */
  virtual bool v6only() const PURE;

  /**
   * @return Ipv4 address from Ipv4-compatible Ipv6 address. Return `nullptr`
   * if the Ipv6 address isn't Ipv4 mapped.
   */
  virtual InstanceConstSharedPtr v4CompatibleAddress() const PURE;

  /**
   * @return Ipv6 address that has no scope/zone identifier. Return `nullptr`
   * if the conversion failed.
   */
  virtual InstanceConstSharedPtr addressWithoutScopeId() const PURE;
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
 * Interface for a generic Pipe address.
 */
class Pipe {
public:
  virtual ~Pipe() = default;
  /**
   * @return abstract namespace flag.
   */
  virtual bool abstractNamespace() const PURE;

  /**
   * @return pipe mode.
   */
  virtual mode_t mode() const PURE;
};

/**
 * Interface for a generic internal address.
 */
class EnvoyInternalAddress {
public:
  virtual ~EnvoyInternalAddress() = default;

  /**
   * @return The unique id of the internal address. If the address represents the destination
   * internal listener, the address id is that listener name.
   */
  virtual const std::string& addressId() const PURE;

  /**
   * @return The optional endpoint id of the internal address.
   */
  virtual const std::string& endpointId() const PURE;
};

enum class Type { Ip, Pipe, EnvoyInternal };

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
   * @return the envoy internal address information IFF type() ==
   * Type::EnvoyInternal, otherwise nullptr.
   */
  virtual const EnvoyInternalAddress* envoyInternalAddress() const PURE;

  /**
   * @return the underlying structure wherein the address is stored. Return nullptr if the address
   * type is internal address.
   */
  virtual const sockaddr* sockAddr() const PURE;

  /**
   * @return length of the address container.
   */
  virtual socklen_t sockAddrLen() const PURE;

  /**
   * @return the type of address.
   */
  virtual Type type() const PURE;

  /**
   * Return the address type in string_view. The returned type name is used to find the
   * ClientConnectionFactory.
   */
  virtual absl::string_view addressType() const PURE;

  /**
   * @return SocketInterface to be used with the address.
   */
  virtual const Network::SocketInterface& socketInterface() const PURE;
};

} // namespace Address
} // namespace Network
} // namespace Envoy
