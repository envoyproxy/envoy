#pragma once

#include <sys/socket.h>
#include <sys/types.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/runtime/runtime.h"

#include "absl/numeric/int128.h"

namespace Envoy {
namespace Network {
namespace Address {

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
   * @return the port associated with the address. Port may be zero if not specified or applicable.
   *         The port is in host byte order.
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
   * physical/resolved address.
   *
   * This string will be in the following format:
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
   * @return the platform error code.
   */
  virtual int bind(int fd) const PURE;

  /**
   * Connect a socket to this address. The socket should have been created with a call to socket()
   * on this object.
   * @param fd supplies the platform socket handle.
   * @return the platform error code.
   */
  virtual int connect(int fd) const PURE;

  /**
   * @return the IP address information IFF type() == Type::Ip, otherwise nullptr.
   */
  virtual const Ip* ip() const PURE;

  /**
   * Create a socket for this address.
   * @param type supplies the socket type to create.
   * @return the platform error code.
   */
  virtual int socket(SocketType type) const PURE;

  /**
   * @return the type of address.
   */
  virtual Type type() const PURE;
};

typedef std::shared_ptr<const Instance> InstanceConstSharedPtr;

/**
 * Interface for all network addresses that may include ranges of ports.
 */
class InstanceRange {
public:
  virtual ~InstanceRange() {}

  virtual bool operator==(const InstanceRange& rhs) const PURE;
  bool operator!=(const InstanceRange& rhs) const { return !operator==(rhs); }

  /**
   * @return a human readable string for the address that represents the
   * physical/resolved address.
   *
   * This string will be in the following format:
   * For IPv4 addresses: "1.2.3.4:80"
   * For IPv6 addresses: "[1234:5678::9]:443"
   * For pipe addresses: "/foo"
   */
  virtual const std::string& asString() const PURE;

  /**
   * @return a human readable string for the address that represents the
   * logical/unresolved name.
   *
   * This string has a source-dependent format and should be obviously
   * convertable to a set of names for Address::instances that can be resolved by
   * a Network::Address::Resolver.
   */
  virtual const std::string& logicalName() const PURE;

  /**
   * Bind a socket to this address range. The socket should have been created with a call to
   * socket() on an Instance of the same address family.
   * @param fd supplies the platform socket handle.
   * @param random may be used to randomize the port in the port range.
   * @return the platform error code.
   */
  virtual int bind(int fd, Runtime::RandomGenerator& random) const PURE;

  /**
   * @return the IP address information IFF type() == Type::Ip, otherwise nullptr.
   * ip()->port() will be invalid; use starting_port() and ending_port() instead.
   */
  virtual const Ip* ip() const PURE;

  /**
   * @return the starting port of the port range (inclusive).
   * If this is a degenerate pipe InstanceRange (i.e. an address instance referring to
   * a single pipe) this value will be 0.
   */
  virtual uint32_t starting_port() const PURE;

  /**
   * @return the ending port of the port range (inclusive).
   * If this is a degenerate pipe InstanceRange (i.e. an address instance referring to
   * a single pipe) this value will be 0.
   */
  virtual uint32_t ending_port() const PURE;

  /**
   * Create a socket for this address.
   * @param type supplies the socket type to create.
   * @return the platform error code.
   */
  virtual int socket(SocketType type) const PURE;

  /**
   * @return the type of address.
   */
  virtual Type type() const PURE;
};

typedef std::shared_ptr<const InstanceRange> InstanceRangeConstSharedPtr;

} // namespace Address
} // namespace Network
} // namespace Envoy
