#pragma once

#include <string>
#include <vector>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/upstream/local_address_selector/v3/default_local_address_selector.pb.h"
#include "envoy/network/address.h"
#include "envoy/network/socket.h"
#include "envoy/registry/registry.h"
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
      std::vector<::Envoy::Upstream::UpstreamLocalAddress> upstream_local_addresses,
      absl::optional<std::string> cluster_name);

  // UpstreamLocalAddressSelector
  UpstreamLocalAddress getUpstreamLocalAddressImpl(
      const Network::Address::InstanceConstSharedPtr& endpoint_address) const override;

private:
  std::vector<UpstreamLocalAddress> upstream_local_addresses_;
};

class DefaultUpstreamLocalAddressSelectorFactory : public UpstreamLocalAddressSelectorFactory {
public:
  std::string name() const override;

  UpstreamLocalAddressSelectorConstSharedPtr createLocalAddressSelector(
      std::vector<::Envoy::Upstream::UpstreamLocalAddress> upstream_local_addresses,
      absl::optional<std::string> cluster_name) const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::config::upstream::local_address_selector::v3::DefaultLocalAddressSelector>();
  }
};

DECLARE_FACTORY(DefaultUpstreamLocalAddressSelectorFactory);

} // namespace Upstream
} // namespace Envoy
