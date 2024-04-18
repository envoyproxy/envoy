#pragma once

#include <string>
#include <vector>

#include "envoy/upstream/upstream.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Upstream {

/**
 * Default implementation of UpstreamLocalAddressSelector.
 *
 * See :ref:`DefaultLocalAddressSelector
 * <envoy_v3_api_msg_config.upstream.local_address_selector.v3.DefaultLocalAddressSelector>`
 * for a description of the behavior of this implementation.
 */
class DefaultUpstreamLocalAddressSelector : public UpstreamLocalAddressSelector {
public:
  DefaultUpstreamLocalAddressSelector(
      std::vector<::Envoy::Upstream::UpstreamLocalAddress>&& upstream_local_addresses);

  // UpstreamLocalAddressSelector
  UpstreamLocalAddress getUpstreamLocalAddressImpl(
      const Network::Address::InstanceConstSharedPtr& endpoint_address) const override;

private:
  std::vector<UpstreamLocalAddress> upstream_local_addresses_;
};

} // namespace Upstream
} // namespace Envoy
