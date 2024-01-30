#include "source/common/upstream/default_local_address_selector.h"

#include <string>

namespace Envoy {
namespace Upstream {

DefaultUpstreamLocalAddressSelector::DefaultUpstreamLocalAddressSelector(
    std::vector<::Envoy::Upstream::UpstreamLocalAddress>&& upstream_local_addresses)
    : upstream_local_addresses_(std::move(upstream_local_addresses)) {
  // If bind config is not provided, we insert at least one
  // ``UpstreamLocalAddress`` with null address.
  ASSERT(!upstream_local_addresses_.empty());
}

UpstreamLocalAddress DefaultUpstreamLocalAddressSelector::getUpstreamLocalAddressImpl(
    const Network::Address::InstanceConstSharedPtr& endpoint_address) const {
  for (auto& local_address : upstream_local_addresses_) {
    if (local_address.address_ == nullptr) {
      continue;
    }

    // Invalid addresses should have been rejected while parsing the bind
    // config.
    ASSERT(local_address.address_->ip() != nullptr);
    if (endpoint_address->ip() != nullptr &&
        local_address.address_->ip()->version() == endpoint_address->ip()->version()) {
      return local_address;
    }
  }

  return upstream_local_addresses_[0];
}

} // namespace Upstream
} // namespace Envoy
