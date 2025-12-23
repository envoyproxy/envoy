#pragma once

#include "envoy/extensions/filters/network/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/filters/network/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/network/common/factory_base.h"

namespace Envoy {
namespace Server {
namespace Configuration {

using FilterConfig =
    envoy::extensions::filters::network::dynamic_modules::v3::DynamicModuleNetworkFilter;

class DynamicModuleNetworkFilterConfigFactory
    : public Extensions::NetworkFilters::Common::ExceptionFreeFactoryBase<FilterConfig> {
public:
  DynamicModuleNetworkFilterConfigFactory()
      : ExceptionFreeFactoryBase("envoy.filters.network.dynamic_modules") {}

private:
  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const FilterConfig& proto_config,
                                    FactoryContext& context) override;

  bool isTerminalFilterByProtoTyped(const FilterConfig&, ServerFactoryContext&) override {
    // Network filters can be terminal or not, but dynamic modules don't have explicit terminal
    // support like HTTP filters.
    return false;
  }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
