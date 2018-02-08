#pragma once

#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <array>
#include <cstdint>
#include <string>

#include "envoy/network/address.h"

namespace Envoy {
namespace Network {
namespace Address {

/**
 * Convert an address in the form of the socket address struct defined by Posix, Linux, etc. into
 * a Network::Address::Instance and return a pointer to it. Raises an EnvoyException on failure.
 * @param ss a valid address with family AF_INET, AF_INET6 or AF_UNIX.
 * @param len length of the address (e.g. from accept, getsockname or getpeername). If len > 0,
 *        it is used to validate the structure contents; else if len == 0, it is ignored.
 * @param v6only disable IPv4-IPv6 mapping for IPv6 addresses?
 * @return InstanceConstSharedPtr the address.
 */
Address::InstanceConstSharedPtr addressFromSockAddr(const sockaddr_storage& ss, socklen_t len,
                                                    bool v6only = true);

/**
 * Obtain an address from a bound file descriptor. Raises an EnvoyException on failure.
 * @param fd file descriptor.
 * @return InstanceConstSharedPtr for bound address.
 */
InstanceConstSharedPtr addressFromFd(int fd);

/**
 * Obtain the address of the peer of the socket with the specified file descriptor.
 * Raises an EnvoyException on failure.
 * @param fd file descriptor.
 * @return InstanceConstSharedPtr for peer address.
 */
InstanceConstSharedPtr peerAddressFromFd(int fd);

/**
 * Base class for all address types.
 */
class InstanceBase : public Instance {
public:
  // Network::Address::Instance
  bool operator==(const Instance& rhs) const override { return asString() == rhs.asString(); }
  const std::string& asString() const override { return friendly_name_; }
  // Default logical name is the human-readable name.
  const std::string& logicalName() const override { return asString(); }
  Type type() const override { return type_; }

protected:
  InstanceBase(Type type) : type_(type) {}
  int socketFromSocketType(SocketType type) const;

  std::string friendly_name_;

private:
  const Type type_;
};

/**
 * Implementation of an IPv4 address.
 */
class Ipv4Instance : public InstanceBase {
public:
  /**
   * Construct from an existing unix IPv4 socket address (IP v4 address and port).
   */
  explicit Ipv4Instance(const sockaddr_in* address);

  /**
   * Construct from a string IPv4 address such as "1.2.3.4". Port will be unset/0.
   */
  explicit Ipv4Instance(const std::string& address);

  /**
   * Construct from a string IPv4 address such as "1.2.3.4" as well as a port.
   */
  Ipv4Instance(const std::string& address, uint32_t port);

  /**
   * Construct from a port. The IPv4 address will be set to "any" and is suitable for binding
   * a port to any available address.
   */
  explicit Ipv4Instance(uint32_t port);

  // Network::Address::Instance
  int bind(int fd) const override;
  int connect(int fd) const override;
  const Ip* ip() const override { return &ip_; }
  int socket(SocketType type) const override;

private:
  struct Ipv4Helper : public Ipv4 {
    uint32_t address() const override { return address_.sin_addr.s_addr; }

    sockaddr_in address_;
  };

  struct IpHelper : public Ip {
    const std::string& addressAsString() const override { return friendly_address_; }
    bool isAnyAddress() const override { return ipv4_.address_.sin_addr.s_addr == INADDR_ANY; }
    bool isUnicastAddress() const override {
      return !isAnyAddress() && (ipv4_.address_.sin_addr.s_addr != INADDR_BROADCAST) &&
             // inlined IN_MULTICAST() to avoid byte swapping
             !((ipv4_.address_.sin_addr.s_addr & htonl(0xf0000000)) == htonl(0xe0000000));
    }
    const Ipv4* ipv4() const override { return &ipv4_; }
    const Ipv6* ipv6() const override { return nullptr; }
    uint32_t port() const override { return ntohs(ipv4_.address_.sin_port); }
    IpVersion version() const override { return IpVersion::v4; }

    Ipv4Helper ipv4_;
    std::string friendly_address_;
  };

  IpHelper ip_;
};

/**
 * Implementation of an IPv6 address.
 */
class Ipv6Instance : public InstanceBase {
public:
  /**
   * Construct from an existing unix IPv6 socket address (IP v6 address and port).
   */
  Ipv6Instance(const sockaddr_in6& address, bool v6only = true);

  /**
   * Construct from a string IPv6 address such as "12:34::5". Port will be unset/0.
   */
  explicit Ipv6Instance(const std::string& address);

  /**
   * Construct from a string IPv6 address such as "12:34::5" as well as a port.
   */
  Ipv6Instance(const std::string& address, uint32_t port);

  /**
   * Construct from a port. The IPv6 address will be set to "any" and is suitable for binding
   * a port to any available address.
   */
  explicit Ipv6Instance(uint32_t port);

  // Network::Address::Instance
  int bind(int fd) const override;
  int connect(int fd) const override;
  const Ip* ip() const override { return &ip_; }
  int socket(SocketType type) const override;

private:
  struct Ipv6Helper : public Ipv6 {
    Ipv6Helper() { memset(&address_, 0, sizeof(address_)); }
    absl::uint128 address() const override;
    uint32_t port() const;

    std::string makeFriendlyAddress() const;

    sockaddr_in6 address_;
  };

  struct IpHelper : public Ip {
    const std::string& addressAsString() const override { return friendly_address_; }
    bool isAnyAddress() const override {
      return 0 == memcmp(&ipv6_.address_.sin6_addr, &in6addr_any, sizeof(struct in6_addr));
    }
    bool isUnicastAddress() const override {
      return !isAnyAddress() && !IN6_IS_ADDR_MULTICAST(&ipv6_.address_.sin6_addr);
    }
    const Ipv4* ipv4() const override { return nullptr; }
    const Ipv6* ipv6() const override { return &ipv6_; }
    uint32_t port() const override { return ipv6_.port(); }
    IpVersion version() const override { return IpVersion::v6; }

    Ipv6Helper ipv6_;
    std::string friendly_address_;
    // Is IPv4 compatibility (https://tools.ietf.org/html/rfc3493#page-11) disabled?
    // Default initialized to true to preserve extant Envoy behavior where we don't explicitly set
    // this in the constructor.
    bool v6only_{true};
  };

  IpHelper ip_;
};

/**
 * Implementation of a pipe address (unix domain socket on unix).
 */
class PipeInstance : public InstanceBase {
public:
  /**
   * Construct from an existing unix address.
   */
  explicit PipeInstance(const sockaddr_un* address, socklen_t ss_len);

  /**
   * Construct from a string pipe path.
   */
  explicit PipeInstance(const std::string& pipe_path);

  // Network::Address::Instance
  int bind(int fd) const override;
  int connect(int fd) const override;
  const Ip* ip() const override { return nullptr; }
  int socket(SocketType type) const override;

private:
  sockaddr_un address_;
  // For abstract namespaces.
  bool abstract_namespace_{false};
  uint32_t address_length_{0};
};

} // namespace Address
} // namespace Network
} // namespace Envoy
