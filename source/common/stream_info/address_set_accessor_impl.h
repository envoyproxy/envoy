#pragma once

#include "envoy/stream_info/address_set_accessor.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace StreamInfo {

/**
 * A FilterState object that holds a set of network addresses.
 */
class AddressSetAccessorImpl : public AddressSetAccessor {
public:
  void add(Network::Address::InstanceConstSharedPtr address) override {
    addresses_.emplace(address);
  }

  void clear() override { addresses_.clear(); }

  void iterate(const std::function<bool(const Network::Address::InstanceConstSharedPtr& address)>&
                   fn) const override {
    for (const auto& address : addresses_) {
      if (!fn(address)) {
        break;
      }
    }
  }

  static const std::string& key() {
    CONSTRUCT_ON_FIRST_USE(std::string, "filter_state_key.address_set_accessor");
  }

private:
  absl::flat_hash_set<Network::Address::InstanceConstSharedPtr> addresses_;
};

} // namespace StreamInfo
} // namespace Envoy
