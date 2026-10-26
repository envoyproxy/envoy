#include "source/extensions/filters/udp/udp_proxy/session_filters/ext_authz/config.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/udp/udp_proxy/session_filters/ext_authz/ext_authz.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace ExtAuthz {

ExtAuthzFilterConfigFactory::ExtAuthzFilterConfigFactory() : FactoryBase(std::string(FilterName)) {}

FilterFactoryCb ExtAuthzFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const FilterConfig& proto_config, Server::Configuration::FactoryContext& context) {
  ConfigSharedPtr filter_config =
      std::make_shared<Config>(proto_config, context.scope(), context.serverFactoryContext());

  return [filter_config](Network::UdpSessionFilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addReadFilter(std::make_shared<Filter>(filter_config, filter_config->createClient()));
  };
}

/**
 * Static registration for the UDP session ext_authz filter. @see RegisterFactory.
 */
REGISTER_FACTORY(ExtAuthzFilterConfigFactory, NamedUdpSessionFilterConfigFactory);

} // namespace ExtAuthz
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
