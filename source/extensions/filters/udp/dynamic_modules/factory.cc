#include "source/extensions/filters/udp/dynamic_modules/factory.h"

#include "source/extensions/filters/udp/dynamic_modules/filter.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DynamicModules {

Network::UdpListenerFilterFactoryCb
DynamicModuleUdpListenerFilterConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& config, Server::Configuration::ListenerFactoryContext&) {
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
      proto_config, std::move(dynamic_module));

  return [filter_config](Network::UdpListenerFilterManager& filter_manager,
                         Network::UdpReadFilterCallbacks& callbacks) -> void {
    filter_manager.addReadFilter(
        std::make_unique<DynamicModuleUdpListenerFilter>(callbacks, filter_config));
  };
}

REGISTER_FACTORY(DynamicModuleUdpListenerFilterConfigFactory,
                 Server::Configuration::NamedUdpListenerFilterConfigFactory);

} // namespace DynamicModules
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
