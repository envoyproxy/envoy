#pragma once

#include <sys/socket.h>
#include <sys/types.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/pure.h"

#include "absl/numeric/int128.h"

namespace Envoy {
namespace Network {
namespace Address {

/**
 * Result holds the rc and errno values resulting from a system call.
 */
struct Result {

  /**
   * The return code from the system call.
   */
  int rc;

  /**
   * The errno value as captured after the system call.
   */
  int error;
};

/**
 * Interface for an Ipv4 address.
 */
class Ipv4 {
public:
  virtual ~Ipv4() {}

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
  virtual ~Ipv6() {}

  /**
   * @return the absl::uint128 IPv6 address in network byte order.
   */
  virtual absl::uint128 address() const PURE;
};

enum class IpVersion { v4, v6 };

/**
 * Interface for a generic IP address.
 */
class Ip {
public:
  virtual ~Ip() {}

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

enum class Type { Ip, Pipe };
enum class SocketType { Stream, Datagram };

/**
 * Interface for all network addresses.
 */
class Instance {
public:
  virtual ~Instance() {}

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
   * @return a human readable string for the address that represents the
   * logical/unresolved name.
   *
   * This string has a source-dependent format and should preserve the original
   * name for Address::Instances resolved by a Network::Address::Resolver.
   */
  virtual const std::string& logicalName() const PURE;

  /**
   * Bind a socket to this address. The socket should have been created with a call to socket() on
   * an Instance of the same address family.
   * @param fd supplies the platform socket handle.
   * @return a Result with rc = 0 for success and rc = -1 for failure. If the call is successful,
   *   the error value shouldn't be used.
   */
  virtual Result bind(int fd) const PURE;

  /**
   * Connect a socket to this address. The socket should have been created with a call to socket()
   * on this object.
   * @param fd supplies the platform socket handle.
   * @return a Result with rc = 0 for success and rc = -1 for failure. If the call is successful,
   *   the error value shouldn't be used.
   */
  virtual Result connect(int fd) const PURE;

  /**
   * @return the IP address information IFF type() == Type::Ip, otherwise nullptr.
   */
  virtual const Ip* ip() const PURE;

  /**
   * Create a socket for this address.
   * @param type supplies the socket type to create.
   * @return a Result with rc = fd for success and rc = -1 for failure, where fd represents the
   *   file descriptor naming the socket. If a valid file descriptor is returned, the error value
   *   shouldn't be used.
   */
  virtual Result socket(SocketType type) const PURE;

  /**
   * @return the type of address.
   */
  virtual Type type() const PURE;
};

typedef std::shared_ptr<const Instance> InstanceConstSharedPtr;

} // namespace Address
} // namespace Network
} // namespace Envoy
