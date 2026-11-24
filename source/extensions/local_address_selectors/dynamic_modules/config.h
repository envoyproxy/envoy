#pragma once

#include <string>

#include "envoy/extensions/local_address_selectors/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/local_address_selectors/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Extensions {
namespace LocalAddressSelectors {
namespace DynamicModules {

class DynamicModuleLocalAddressSelectorFactory
    : public Upstream::UpstreamLocalAddressSelectorFactory {
public:
  std::string name() const override {
    return "envoy.upstream.local_address_selector.dynamic_modules";
  }

  // The selector needs its config, so Envoy must go through createLocalAddressSelectorWithConfig().
  absl::StatusOr<Upstream::UpstreamLocalAddressSelectorConstSharedPtr>
  createLocalAddressSelector(std::vector<Upstream::UpstreamLocalAddress> upstream_local_addresses,
                             std::optional<std::string> cluster_name) const override;

  absl::StatusOr<Upstream::UpstreamLocalAddressSelectorConstSharedPtr>
  createLocalAddressSelectorWithConfig(
      const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context,
      Stats::Scope& stats_scope,
      std::vector<Upstream::UpstreamLocalAddress> upstream_local_addresses,
      std::optional<std::string> cluster_name) const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::local_address_selectors::dynamic_modules::v3::
                                DynamicModuleLocalAddressSelector>();
  }
};

DECLARE_FACTORY(DynamicModuleLocalAddressSelectorFactory);

} // namespace DynamicModules
} // namespace LocalAddressSelectors
} // namespace Extensions
} // namespace Envoy
