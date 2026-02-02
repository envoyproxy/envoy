#include "source/extensions/filters/udp/dynamic_modules/factory.h"

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

  auto dynamic_module_or_error = Extensions::DynamicModules::newDynamicModuleByName(
      proto_config.dynamic_module_config().name(),
      proto_config.dynamic_module_config().do_not_close(),
      proto_config.dynamic_module_config().load_globally());

  if (!dynamic_module_or_error.ok()) {
    throw EnvoyException(std::string(dynamic_module_or_error.status().message()));
  }

  auto dynamic_module = std::move(dynamic_module_or_error.value());

  auto filter_config = std::make_shared<DynamicModuleUdpListenerFilterConfig>(
      proto_config, std::move(dynamic_module), context.scope());

  // Determine the metrics namespace for registration.
  const auto& module_config = proto_config.dynamic_module_config();
  const std::string metrics_namespace = module_config.metrics_namespace().empty()
                                            ? std::string(DefaultMetricsNamespace)
                                            : module_config.metrics_namespace();

  // Register the metrics namespace as a custom stat namespace unless the user wants to include
  // the namespace in the stats output. When registered, the namespace prefix is stripped from
  // /stats endpoints and no envoy_ prefix is added in prometheus output.
  if (!module_config.include_metrics_namespace_in_stats_output()) {
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
