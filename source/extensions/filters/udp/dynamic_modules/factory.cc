#include "source/extensions/filters/udp/dynamic_modules/factory.h"

#include "source/common/runtime/runtime_features.h"
#include "source/extensions/filters/udp/dynamic_modules/filter.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DynamicModules {

Network::UdpListenerFilterFactoryCb
DynamicModuleUdpListenerFilterConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& config, Server::Configuration::ListenerFactoryContext& context) {
  const auto& proto_config = dynamic_cast<
      const envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter&>(
      config);

  // UDP listener filters do not support remote module sources, so no init manager or async callback
  // is passed; only the synchronous local-file and by-name paths can succeed here.
  auto load_result = Extensions::DynamicModules::newDynamicModuleByConfig(
      proto_config.dynamic_module_config(), proto_config.filter_name(),
      context.serverFactoryContext());
  if (!load_result.ok()) {
    throw EnvoyException(std::string(load_result.status().message()));
  }

  auto dynamic_module = std::move(load_result->loaded);

  auto filter_config = std::make_shared<DynamicModuleUdpListenerFilterConfig>(
      proto_config, std::move(dynamic_module), context.scope());

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

  return [filter_config](Network::UdpListenerFilterManager& filter_manager,
                         Network::UdpReadFilterCallbacks& callbacks) -> void {
    const std::string& worker_name = callbacks.udpListener().dispatcher().name();
    auto pos = worker_name.find_first_of('_');
    ENVOY_BUG(pos != std::string::npos, "worker name is not in expected format worker_{index}");
    uint32_t worker_index;
    if (!absl::SimpleAtoi(worker_name.substr(pos + 1), &worker_index)) {
      IS_ENVOY_BUG("failed to parse worker index from name");
    }
    filter_manager.addReadFilter(
        std::make_unique<DynamicModuleUdpListenerFilter>(callbacks, filter_config, worker_index));
  };
}

REGISTER_FACTORY(DynamicModuleUdpListenerFilterConfigFactory,
                 Server::Configuration::NamedUdpListenerFilterConfigFactory);

} // namespace DynamicModules
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
