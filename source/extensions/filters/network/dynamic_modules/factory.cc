#include "source/extensions/filters/network/dynamic_modules/factory.h"

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/network/dynamic_modules/filter.h"
#include "source/extensions/filters/network/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

absl::StatusOr<Network::FilterFactoryCb>
DynamicModuleNetworkFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const FilterConfig& proto_config, FactoryContext& context) {

  const auto& module_config = proto_config.dynamic_module_config();
  auto dynamic_module = Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close(), module_config.load_globally());
  if (!dynamic_module.ok()) {
    return absl::InvalidArgumentError("Failed to load dynamic module: " +
                                      std::string(dynamic_module.status().message()));
  }

  std::string config;
  if (proto_config.has_filter_config()) {
    auto config_or_error = MessageUtil::anyToBytes(proto_config.filter_config());
    RETURN_IF_NOT_OK_REF(config_or_error.status());
    config = std::move(config_or_error.value());
  }

  absl::StatusOr<
      Envoy::Extensions::DynamicModules::NetworkFilters::DynamicModuleNetworkFilterConfigSharedPtr>
      filter_config =
          Envoy::Extensions::DynamicModules::NetworkFilters::newDynamicModuleNetworkFilterConfig(
              proto_config.filter_name(), config, std::move(dynamic_module.value()),
              context.serverFactoryContext().clusterManager());

  if (!filter_config.ok()) {
    return absl::InvalidArgumentError("Failed to create filter config: " +
                                      std::string(filter_config.status().message()));
  }

  return [config = filter_config.value()](Network::FilterManager& filter_manager) -> void {
    auto filter = std::make_shared<
        Envoy::Extensions::DynamicModules::NetworkFilters::DynamicModuleNetworkFilter>(config);
    filter->initializeInModuleFilter();
    filter_manager.addFilter(filter);
  };
}

/**
 * Static registration for the dynamic modules network filter.
 */
REGISTER_FACTORY(DynamicModuleNetworkFilterConfigFactory, NamedNetworkFilterConfigFactory);

} // namespace Configuration
} // namespace Server
} // namespace Envoy
