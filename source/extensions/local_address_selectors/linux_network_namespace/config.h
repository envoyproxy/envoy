#pragma once

#include <string>

#include "envoy/registry/registry.h"
#include "envoy/upstream/upstream.h"

#include "envoy/config/upstream/local_address_selector/v3/default_local_address_selector.pb.h"

namespace Envoy {
namespace Extensions {
namespace LocalAddressSelectors {
namespace LinuxNetworkNamespace {

class NamespaceLocalAddressSelectorFactory : public Upstream::UpstreamLocalAddressSelectorFactory {
public:
  std::string name() const override;

  absl::StatusOr<UpstreamLocalAddressSelectorConstSharedPtr> createLocalAddressSelector(
      std::vector<::Envoy::Upstream::UpstreamLocalAddress> upstream_local_addresses,
      absl::optional<std::string> cluster_name) const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::config::upstream::local_address_selector::v3::DefaultLocalAddressSelector>();
  }
};

DECLARE_FACTORY(DefaultUpstreamLocalAddressSelectorFactory);

} // namespace LinuxNetworkNamespace
} // namespace LocalAddressSelectors
} // namespace Extensions
} // namespace Envoy
