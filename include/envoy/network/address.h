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

/*
 * Parse an internet host address (IPv4 or IPv6) and create an Instance from it.
 * The address must not include a port number.
 * @param ipAddr string to be parsed as an internet address.
 * @return pointer to the Instance, or nullptr if unable to parse the address.
 */
InstancePtr parseInternetAddress(const std::string& ipAddr);

/*
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
   * @return Ipv4 address data IFF length >= 0 and version() == IpVersion::v4, otherwise nullptr.
   */
  const Ipv4* ipv4() const;

  /**
   * @return Ipv6 address data IFF length >= 0 and version() == IpVersion::v6, otherwise nullptr.
   */
  const Ipv6* ipv6() const;

  /**
   * @return the number of bits of the address that are included in the mask.
   * -1 if uninitialized or invalid, else in the range 0 to 32 for IPv4, and 0 to 128 for IPv6.
   */
  int length() const;

  /**
   * @return the version of IP address.
   */
  IpVersion version() const;

  /**
   * @return true if the address argument is in the range of this object.
   * false if not, including if the range is uninitialized or if the argument is not of the
   * same IpVersion.
   */
  bool isInRange(const InstancePtr& address) const;

  /**
   * @return a human readable string for the range.
   *
   * This string will be in the following format:
   * For IPv4 ranges: "1.2.3.4/32" or "10.240.0.0/16"
   * For IPv6 ranges: "1234:5678::f/128" or "1234:5678::/64"
   */
  std::string asString() const;

  /**
   * @return true if this instance is valid; address != nullptr && length is appropriate
   * for the IP version (these are checked during construction, and reduced down to a
   * check of the length).
   */
  bool isValid() const { return length_ >= 0; }

  /**
   * @return an CidrRange instance with the specified address and length, modified so that
   * the only bits that might be non-zero are in the high-order length bits, and so that
   * length is in the appropriate range (0 to 32 for IPv4, 0 to 128 for IPv6). If the
   * the address or length is invalid, then the range will be invalid (i.e. length == -1).
   */
  static CidrRange construct(const InstancePtr& address, int length);
  static CidrRange construct(const std::string& address, int length);

  /**
   * Constructs an CidrRange from a string with this format (same as returned
   * by CidrRange::asString above):
   *      <address>/<length>    e.g. "10.240.0.0/16" or "1234:5678::/64"
   * @return an CidrRange instance with the specified address and length if parsed successfully,
   * else with no address and a length of -1.
   */
  static CidrRange construct(const std::string& range);

private:
  CidrRange(const InstancePtr& address, int length);

  InstancePtr address_;
  int length_;
};

} // Address
} // Network
