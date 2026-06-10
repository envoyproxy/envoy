#include "source/extensions/filters/network/dynamic_modules/factory.h"

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/filters/network/dynamic_modules/filter.h"
#include "source/extensions/filters/network/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

absl::StatusOr<Network::FilterFactoryCb>
DynamicModuleNetworkFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const FilterConfig& proto_config, FactoryContext& context) {

  const auto& module_config = proto_config.dynamic_module_config();
  // Network filters do not support remote module sources, so no init manager or async callback is
  // passed; only the synchronous local-file and by-name paths can succeed here.
  auto load_result = Extensions::DynamicModules::newDynamicModuleByConfig(
      module_config, proto_config.filter_name(), context.serverFactoryContext());
  RETURN_IF_NOT_OK_REF(load_result.status());
  auto dynamic_module = std::move(load_result->loaded);

  std::string config;
  if (proto_config.has_filter_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.filter_config());
    RETURN_IF_NOT_OK_REF(config_or_error.status());
    config = std::move(config_or_error.value());
  }

  // Use configured metrics namespace or fall back to the default.
  const std::string metrics_namespace =
      module_config.metrics_namespace().empty()
          ? std::string(Extensions::DynamicModules::NetworkFilters::DefaultMetricsNamespace)
          : module_config.metrics_namespace();

  absl::StatusOr<
      Envoy::Extensions::DynamicModules::NetworkFilters::DynamicModuleNetworkFilterConfigSharedPtr>
      filter_config =
          Envoy::Extensions::DynamicModules::NetworkFilters::newDynamicModuleNetworkFilterConfig(
              proto_config.filter_name(), config, metrics_namespace, std::move(dynamic_module),
              context.serverFactoryContext().clusterManager(),
              context.serverFactoryContext().scope(),
              context.serverFactoryContext().mainThreadDispatcher());

  if (!filter_config.ok()) {
    return absl::InvalidArgumentError("Failed to create filter config: " +
                                      std::string(filter_config.status().message()));
  }

  // When the runtime guard is enabled, register the metrics namespace as a custom stat namespace.
  // This causes the namespace prefix to be stripped from prometheus output and no envoy_ prefix
  // is added. This is the legacy behavior for backward compatibility.
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix")) {
    context.serverFactoryContext().api().customStatNamespaces().registerStatNamespace(
        metrics_namespace);
  }

  return [config = filter_config.value()](Network::FilterManager& filter_manager) -> void {
    auto filter = std::make_shared<
        Envoy::Extensions::DynamicModules::NetworkFilters::DynamicModuleNetworkFilter>(config);
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
