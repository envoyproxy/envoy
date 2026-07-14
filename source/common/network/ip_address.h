#pragma once

#include "envoy/network/address.h"

namespace Envoy {
namespace Network {

/**
 * IP Address Object that can be used to store the IP address in the filter state
 */
class IPAddressObject : public Address::InstanceAccessor {
public:
  IPAddressObject(Network::Address::InstanceConstSharedPtr address)
      : Address::InstanceAccessor(address) {}

  std::optional<std::string> serializeAsString() const override {
    const auto ip = getIp();
    return ip ? std::make_optional(ip->asString()) : std::nullopt;
  }
};

} // namespace Network
} // namespace Envoy
