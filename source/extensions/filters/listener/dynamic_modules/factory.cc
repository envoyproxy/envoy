#include "source/extensions/filters/listener/dynamic_modules/factory.h"

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"
#include "source/extensions/filters/listener/dynamic_modules/filter.h"
#include "source/extensions/filters/listener/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

Network::ListenerFilterFactoryCb
DynamicModuleListenerFilterConfigFactory::createListenerFilterFactoryFromProto(
    const Protobuf::Message& message,
    const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
    ListenerFactoryContext& context) {

  const auto& proto_config = MessageUtil::downcastAndValidate<const ListenerFilterConfig&>(
      message, context.messageValidationVisitor());

  Server::Configuration::ServerFactoryContext& server_context = context.serverFactoryContext();
  const auto& module_config = proto_config.dynamic_module_config();
  // Listener filters do not support remote module sources, so no init manager or async callback is
  // passed; only the synchronous local-file and by-name paths can succeed here.
  auto load_result = Extensions::DynamicModules::newDynamicModuleByConfig(
      module_config, proto_config.filter_name(), server_context);
  if (!load_result.ok()) {
    throw EnvoyException(std::string(load_result.status().message()));
  }
  auto dynamic_module = std::move(load_result->loaded);

  std::string filter_config_str;
  if (proto_config.has_filter_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.filter_config());
    if (!config_or_error.ok()) {
      Extensions::DynamicModules::incrementLoadFailure(
          server_context, proto_config.filter_name(),
          Extensions::DynamicModules::ConfigInitErrorStat);
      throw EnvoyException("Failed to parse filter config: " +
                           std::string(config_or_error.status().message()));
    }
    filter_config_str = std::move(config_or_error.value());
  }

  // Use configured metrics namespace or fall back to the default.
  const std::string metrics_namespace =
      module_config.metrics_namespace().empty()
          ? std::string(Extensions::DynamicModules::ListenerFilters::DefaultMetricsNamespace)
          : module_config.metrics_namespace();

  auto filter_config =
      Extensions::DynamicModules::ListenerFilters::newDynamicModuleListenerFilterConfig(
          proto_config.filter_name(), filter_config_str, metrics_namespace,
          std::move(dynamic_module), server_context.clusterManager(), context.listenerScope(),
          server_context.mainThreadDispatcher());

  if (!filter_config.ok()) {
    Extensions::DynamicModules::incrementLoadFailure(
        server_context, proto_config.filter_name(),
        Extensions::DynamicModules::ConfigInitErrorStat);
    throw EnvoyException("Failed to create filter config: " +
                         std::string(filter_config.status().message()));
  }

  // When the runtime guard is enabled, register the metrics namespace as a custom stat namespace.
  // This causes the namespace prefix to be stripped from prometheus output and no envoy_ prefix
  // is added. This is the legacy behavior for backward compatibility.
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix")) {
    server_context.api().customStatNamespaces().registerStatNamespace(metrics_namespace);
  }

  return [filter_cfg = filter_config.value(),
          listener_filter_matcher](Network::ListenerFilterManager& filter_manager) -> void {
    // The manager owns filters as a unique_ptr, but the async callout and scheduler paths call
    // shared_from_this. Hold the filter in a shared_ptr and hand the manager a forwarding adapter.
    auto filter =
        std::make_shared<Extensions::DynamicModules::ListenerFilters::DynamicModuleListenerFilter>(
            filter_cfg);
    filter_manager.addAcceptFilter(
        listener_filter_matcher,
        std::make_unique<Extensions::DynamicModules::ListenerFilters::SharedListenerFilterAdapter>(
            std::move(filter)));
  };
}

/**
 * Static registration for the dynamic modules listener filter.
 */
REGISTER_FACTORY(DynamicModuleListenerFilterConfigFactory, NamedListenerFilterConfigFactory);

} // namespace Configuration
} // namespace Server
} // namespace Envoy
