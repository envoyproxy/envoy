#include "source/extensions/filters/network/header_routing/config.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/network/header_routing/filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HeaderRouting {

HeaderRoutingTcpFilterConfigFactory::HeaderRoutingTcpFilterConfigFactory()
    : FactoryBase("envoy.filters.network.header_routing") {}

Network::FilterFactoryCb HeaderRoutingTcpFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const FilterConfig& proto_config, Server::Configuration::FactoryContext& context) {
  HeaderRoutingTcpFilterConfigSharedPtr filter_config =
      std::make_shared<HeaderRoutingTcpFilterConfig>(proto_config, context);

  return [filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<HeaderRoutingTcpFilter>(filter_config));
  };
}

/**
 * Static registration for the header_routing network filter. @see RegisterFactory.
 */
REGISTER_FACTORY(HeaderRoutingTcpFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace HeaderRouting
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
