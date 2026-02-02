#include "source/extensions/filters/listener/dynamic_modules/factory.h"

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
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

  const auto& module_config = proto_config.dynamic_module_config();
  auto dynamic_module = Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close(), module_config.load_globally());
  if (!dynamic_module.ok()) {
    throw EnvoyException("Failed to load dynamic module: " +
                         std::string(dynamic_module.status().message()));
  }

  std::string filter_config_str;
  if (proto_config.has_filter_config()) {
    auto config_or_error = MessageUtil::anyToBytes(proto_config.filter_config());
    if (!config_or_error.ok()) {
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
          std::move(dynamic_module.value()), context.listenerScope(),
          context.serverFactoryContext().mainThreadDispatcher());

  if (!filter_config.ok()) {
    throw EnvoyException("Failed to create filter config: " +
                         std::string(filter_config.status().message()));
  }

  return [filter_cfg = filter_config.value(),
          listener_filter_matcher](Network::ListenerFilterManager& filter_manager) -> void {
    auto filter =
        std::make_unique<Extensions::DynamicModules::ListenerFilters::DynamicModuleListenerFilter>(
            filter_cfg);
    filter_manager.addAcceptFilter(listener_filter_matcher, std::move(filter));
  };
}

/**
 * Static registration for the dynamic modules listener filter.
 */
REGISTER_FACTORY(DynamicModuleListenerFilterConfigFactory, NamedListenerFilterConfigFactory);

} // namespace Configuration
} // namespace Server
} // namespace Envoy
