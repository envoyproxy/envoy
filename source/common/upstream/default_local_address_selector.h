#pragma once

#include <string>
#include <vector>

#include "envoy/upstream/upstream.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Upstream {

/**
 * Default implementation of UpstreamLocalAddressSelector.
 */
class DefaultUpstreamLocalAddressSelector : public UpstreamLocalAddressSelector {
public:
  DefaultUpstreamLocalAddressSelector(
      std::vector<::Envoy::Upstream::UpstreamLocalAddress> upstream_local_addresses);

  // UpstreamLocalAddressSelector
  UpstreamLocalAddress getUpstreamLocalAddressImpl(
      const Network::Address::InstanceConstSharedPtr& endpoint_address) const override;

private:
  std::vector<UpstreamLocalAddress> upstream_local_addresses_;
};

} // namespace Upstream
} // namespace Envoy
