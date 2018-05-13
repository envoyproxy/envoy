#pragma once

#include "envoy/network/address.h"
#include "envoy/runtime/runtime.h"

namespace Envoy {
namespace Network {
namespace Address {

/**
 * IP address + port range
 */
class IpInstanceRange : public Instance {
public:
  IpInstanceRange(InstanceConstSharedPtr&& address_instance, const std::string& range_as_named_port,
                  Runtime::RandomGenerator& random);
  virtual ~IpInstanceRange() {}

  uint32_t startPort() { return start_port_; }
  uint32_t endPort() { return end_port_; }

  // Instance
  bool operator==(const Instance& rhs) const override { return asString() == rhs.asString(); }
  const std::string& asString() const override { return friendly_name_; }
  const std::string& logicalName() const override { return friendly_name_; }
  int bind(int fd) const override;
  int connect(int fd) const override;
  const Ip* ip() const override { return address_instance_->ip(); }
  int socket(SocketType type) const override { return address_instance_->socket(type); }
  virtual Type type() const override { return Type::Ip; }

private:
  // Contains IP address information; port is zero, meaning "unspecified".
  const InstanceConstSharedPtr address_instance_;
  uint32_t start_port_;
  uint32_t end_port_;

  std::string friendly_name_;

  // For use by bind().
  Runtime::RandomGenerator& random_;
};

} // namespace Address
} // namespace Network
} // namespace Envoy
