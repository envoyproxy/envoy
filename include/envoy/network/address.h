#pragma once

#include "envoy/common/pure.h"

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
   * @return the 16-byte IPv6 address in network byte order.
   */
  virtual std::array<uint8_t, 16> address() const PURE;
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
   * @return Ipv4 address data IFF type() == IpVersion::v4, otherwise nullptr.
   */
  virtual const Ipv4* ipv4() const PURE;

  /**
   * @return Ipv6 address data IFF type() == IpVersion::v6, otherwise nullptr.
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

  /**
   * @return a human readable string for the address.
   *
   * This string will be in the following format:
   * For IPv4 addresses: "1.2.3.4:80"
   * For IPv6 addresses: "[1234:5678::9]:443"
   * For pipe addresses: "/foo"
   */
  virtual const std::string& asString() const PURE;

  /**
   * Bind a socket to this address. The socket should have been created with a call to socket() on
   * this object.
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

typedef std::shared_ptr<const Instance> InstancePtr;

} // Address
} // Network
