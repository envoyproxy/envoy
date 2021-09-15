#pragma once

#include "envoy/network/address.h"
#include "envoy/stream_info/filter_state.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace StreamInfo {

/*
 * A FilterState object that wraps a set of network address shared pointers.
 */
struct UpstreamAddressSet : public FilterState::Object {

  void iterate(const std::function<bool(const Network::Address::InstanceConstSharedPtr& address)>&
                   fn) const {
    for (const auto& address : addresses_) {
      if (!fn(address)) {
        break;
      }
    }
  }

  static const std::string& key() {
    CONSTRUCT_ON_FIRST_USE(std::string, "envoy.request.upstream_address_set");
  }

  absl::flat_hash_set<Network::Address::InstanceConstSharedPtr> addresses_;
};

} // namespace StreamInfo
} // namespace Envoy
