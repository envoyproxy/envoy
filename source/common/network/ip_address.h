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

  absl::optional<std::string> serializeAsString() const override {
    return getIp() ? absl::make_optional(getIp()->asString()) : absl::nullopt;
  }
  static const std::string& key();
};

} // namespace Network
} // namespace Envoy
