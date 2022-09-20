#include "contrib/generic_proxy/filters/network/source/config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

Envoy::Network::FilterFactoryCb
Factory::createFilterFactoryFromProtoTyped(const ProxyConfig& proto_config,
                                           Envoy::Server::Configuration::FactoryContext& context) {
  auto config = std::make_shared<FilterConfig>(proto_config, context);
  return [config, &context](Envoy::Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<Filter>(config, context));
  };
}

REGISTER_FACTORY(Factory, Envoy::Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
