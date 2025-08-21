#include "source/extensions/filters/udp/udp_proxy/session_filters/dynamic_forward_proxy/config.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"
#include "source/extensions/filters/udp/udp_proxy/session_filters/dynamic_forward_proxy/proxy_filter.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace DynamicForwardProxy {

DynamicForwardProxyNetworkFilterConfigFactory::DynamicForwardProxyNetworkFilterConfigFactory()
    : FactoryBase("envoy.filters.udp.session.dynamic_forward_proxy") {}

FilterFactoryCb DynamicForwardProxyNetworkFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const FilterConfig& proto_config, Server::Configuration::FactoryContext& context) {

  Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactoryImpl cache_manager_factory(
      context);
  ProxyFilterConfigSharedPtr filter_config(
      std::make_shared<ProxyFilterConfig>(proto_config, cache_manager_factory, context));

  return [filter_config](Network::UdpSessionFilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addReadFilter(std::make_shared<ProxyFilter>(filter_config));
  };
}

/**
 * Static registration for the dynamic_forward_proxy filter. @see RegisterFactory.
 */
REGISTER_FACTORY(DynamicForwardProxyNetworkFilterConfigFactory, NamedUdpSessionFilterConfigFactory);

} // namespace DynamicForwardProxy
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
