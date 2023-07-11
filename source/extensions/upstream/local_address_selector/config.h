#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/upstream/local_address_selector/v3/default_local_address_selector.pb.h"
#include "envoy/upstream/upstream.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Upstream {

class DefaultUpstreamLocalAddressSelectorFactory : public UpstreamLocalAddressSelectorFactory {
public:
  std::string name() const override;

  UpstreamLocalAddressSelectorPtr
  createLocalAddressSelector(const envoy::config::cluster::v3::Cluster& cluster_config,
                             const absl::optional<envoy::config::core::v3::BindConfig>&
                                 bootstrap_bind_config) const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::upstream::local_address_selector::v3::DefaultLocalAddressSelector>();
  }
};

DECLARE_FACTORY(DefaultUpstreamLocalAddressSelectorFactory);

} // namespace Upstream
} // namespace Envoy
