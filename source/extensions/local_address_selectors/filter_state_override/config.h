#pragma once

#include <string>

#include "envoy/extensions/local_address_selectors/filter_state_override/v3/config.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Extensions {
namespace LocalAddressSelectors {
namespace FilterStateOverride {

class NamespaceLocalAddressSelectorFactory : public Upstream::UpstreamLocalAddressSelectorFactory {
public:
  std::string name() const override {
    return "envoy.upstream.local_address_selector.filter_state_override";
  }

  absl::StatusOr<Upstream::UpstreamLocalAddressSelectorConstSharedPtr>
  createLocalAddressSelector(std::vector<Upstream::UpstreamLocalAddress> upstream_local_addresses,
                             absl::optional<std::string> cluster_name) const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::local_address_selectors::filter_state_override::v3::Config>();
  }
};

DECLARE_FACTORY(NamespaceLocalAddressSelectorFactory);

} // namespace FilterStateOverride
} // namespace LocalAddressSelectors
} // namespace Extensions
} // namespace Envoy
