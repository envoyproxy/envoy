#include "source/extensions/filters/udp/udp_proxy/session_filters/header_routing/config.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/udp/udp_proxy/session_filters/header_routing/filter.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace HeaderRouting {

HeaderRoutingUdpFilterConfigFactory::HeaderRoutingUdpFilterConfigFactory()
    : FactoryBase("envoy.filters.udp.session.header_routing") {}

FilterFactoryCb HeaderRoutingUdpFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const FilterConfig& proto_config, Server::Configuration::FactoryContext& context) {
  HeaderRoutingUdpFilterConfigSharedPtr filter_config =
      std::make_shared<HeaderRoutingUdpFilterConfig>(proto_config, context);

  return [filter_config](Network::UdpSessionFilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addReadFilter(std::make_shared<HeaderRoutingUdpFilter>(filter_config));
  };
}

/**
 * Static registration for the header_routing UDP session filter. @see RegisterFactory.
 */
REGISTER_FACTORY(HeaderRoutingUdpFilterConfigFactory, NamedUdpSessionFilterConfigFactory);

} // namespace HeaderRouting
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
