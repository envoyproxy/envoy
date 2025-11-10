#pragma once

#include <string>

#include "envoy/extensions/local_address_selectors/linux_network_namespace/v3/config.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Extensions {
namespace LocalAddressSelectors {
namespace LinuxNetworkNamespace {

class NamespaceLocalAddressSelectorFactory : public Upstream::UpstreamLocalAddressSelectorFactory {
public:
  std::string name() const override {
    return "envoy.upstream.local_address_selector.linux_network_namespace";
  }

  absl::StatusOr<Upstream::UpstreamLocalAddressSelectorConstSharedPtr>
  createLocalAddressSelector(std::vector<Upstream::UpstreamLocalAddress> upstream_local_addresses,
                             absl::optional<std::string> cluster_name) const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::local_address_selectors::linux_network_namespace::v3::Config>();
  }
};

DECLARE_FACTORY(NamespaceLocalAddressSelectorFactory);

} // namespace LinuxNetworkNamespace
} // namespace LocalAddressSelectors
} // namespace Extensions
} // namespace Envoy
