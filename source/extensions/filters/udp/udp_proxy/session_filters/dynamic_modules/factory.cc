#include "source/extensions/filters/udp/udp_proxy/session_filters/dynamic_modules/factory.h"

#include "envoy/registry/registry.h"

#include "source/common/runtime/runtime_features.h"
#include "source/extensions/filters/udp/udp_proxy/session_filters/dynamic_modules/filter.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace DynamicModules {

FilterFactoryCb DynamicModuleUdpSessionFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const FilterConfig& proto_config, Server::Configuration::FactoryContext& context) {
  // UDP session filters do not support remote module sources, so no init manager or async callback
  // is passed; only the synchronous local-file and by-name paths can succeed here.
  auto load_result = Extensions::DynamicModules::newDynamicModuleByConfig(
      proto_config.dynamic_module_config(), proto_config.filter_name(),
      context.serverFactoryContext());
  if (!load_result.ok()) {
    throw EnvoyException(std::string(load_result.status().message()));
  }

  auto dynamic_module = std::move(load_result->loaded);

  auto filter_config = std::make_shared<DynamicModuleUdpSessionFilterConfig>(
      proto_config, std::move(dynamic_module), context.serverFactoryContext().scope(),
      context.serverFactoryContext().timeSource());

  // When the runtime guard is enabled, register the metrics namespace as a custom stat namespace.
  // This causes the namespace prefix to be stripped from prometheus output and no envoy_ prefix
  // is added. This is the legacy behavior for backward compatibility.
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix")) {
    const auto& module_config = proto_config.dynamic_module_config();
    const std::string metrics_namespace = module_config.metrics_namespace().empty()
                                              ? std::string(DefaultMetricsNamespace)
                                              : module_config.metrics_namespace();
    context.serverFactoryContext().api().customStatNamespaces().registerStatNamespace(
        metrics_namespace);
  }

  return [filter_config](Network::UdpSessionFilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addFilter(std::make_shared<DynamicModuleUdpSessionFilter>(filter_config));
  };
}

REGISTER_FACTORY(DynamicModuleUdpSessionFilterConfigFactory, NamedUdpSessionFilterConfigFactory);

} // namespace DynamicModules
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
