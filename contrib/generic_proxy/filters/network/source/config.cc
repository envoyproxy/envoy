#include "contrib/generic_proxy/filters/network/source/config.h"

#include "contrib/generic_proxy/filters/network/source/rds.h"
#include "contrib/generic_proxy/filters/network/source/rds_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

SINGLETON_MANAGER_REGISTRATION(generic_route_config_provider_manager);

Envoy::Network::FilterFactoryCb
Factory::createFilterFactoryFromProtoTyped(const ProxyConfig& proto_config,
                                           Envoy::Server::Configuration::FactoryContext& context) {

  std::shared_ptr<RouteConfigProviderManager> route_config_provider_manager =
      context.singletonManager().getTyped<RouteConfigProviderManager>(
          SINGLETON_MANAGER_REGISTERED_NAME(generic_route_config_provider_manager),
          [&context] { return std::make_shared<RouteConfigProviderManagerImpl>(context.admin()); });

  const auto config =
      std::make_shared<FilterConfig>(proto_config, context, *route_config_provider_manager);
  return [route_config_provider_manager, config,
          &context](Envoy::Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<Filter>(config, context));
  };
}

REGISTER_FACTORY(Factory, Envoy::Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
