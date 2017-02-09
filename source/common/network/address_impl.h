#pragma once

#include "envoy/network/address.h"

#include <sys/un.h>

namespace Network {
namespace Address {

/**
 * Base class for all address types.
 */
class InstanceBase : public Instance {
public:
  // Network::Address::Instance
  bool operator==(const Instance& rhs) const override { return asString() == rhs.asString(); }
  const std::string& asString() const override { return friendly_name_; }
  Type type() const override { return type_; }

protected:
  InstanceBase(Type type) : type_(type) {}
  int flagsFromSocketType(SocketType type) const;

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
   * Construct from an existing unix IPv4 address.
   */
  Ipv4Instance(const sockaddr_in* address);

  /**
   * Construct from a string IPv4 address such as "1.2.3.4". Port will be unset/0.
   */
  Ipv4Instance(const std::string& address);

  /**
   * Construct from a string IPv4 address such as "1.2.3.4" as well as a port.
   */
  Ipv4Instance(const std::string& address, uint32_t port);

  /**
   * Construct from a port. The IPv4 address will be set to "any" and is suitable for binding
   * a port to any available address.
   */
  Ipv4Instance(uint32_t port);

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
    const Ipv4* ipv4() const override { return &ipv4_; }
    uint32_t port() const override { return ntohs(ipv4_.address_.sin_port); }
    IpVersion version() const override { return IpVersion::v4; }

    Ipv4Helper ipv4_;
    std::string friendly_address_;
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
  PipeInstance(const sockaddr_un* address);

  /**
   * Construct from a string pipe path.
   */
  PipeInstance(const std::string& pipe_path);

  // Network::Address::Instance
  int bind(int fd) const override;
  int connect(int fd) const override;
  const Ip* ip() const override { return nullptr; }
  int socket(SocketType type) const override;

private:
  sockaddr_un address_;
};

} // Address
} // Network
