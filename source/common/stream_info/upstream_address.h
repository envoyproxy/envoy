#pragma once

#include <optional>
#include <string>

#include "envoy/network/address.h"
#include "envoy/stream_info/filter_state.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace StreamInfo {

/*
 * A FilterState object that wraps a network address shared pointer.
 */
class UpstreamAddress : public Network::Address::InstanceAccessor {
public:
  UpstreamAddress(Network::Address::InstanceConstSharedPtr ip)
      : Network::Address::InstanceAccessor(ip) {}
  std::optional<std::string> serializeAsString() const override {
    const auto address = getAddress();
    return address ? std::make_optional(address->asString()) : std::nullopt;
  }
  static const std::string& key() {
    CONSTRUCT_ON_FIRST_USE(std::string, "envoy.stream.upstream_address");
  }
};

} // namespace StreamInfo
} // namespace Envoy
