#include "source/extensions/filters/network/sni_dynamic_forward_proxy/config.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"
#include "source/extensions/filters/network/sni_dynamic_forward_proxy/proxy_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniDynamicForwardProxy {

SniDynamicForwardProxyNetworkFilterConfigFactory::SniDynamicForwardProxyNetworkFilterConfigFactory()
    : Common::ExceptionFreeFactoryBase<FilterConfig>(
          NetworkFilterNames::get().SniDynamicForwardProxy) {}

absl::StatusOr<Network::FilterFactoryCb>
SniDynamicForwardProxyNetworkFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const FilterConfig& proto_config, Server::Configuration::FactoryContext& context) {

  Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactoryImpl cache_manager_factory(
      context);

  absl::Status status = absl::OkStatus();
  ProxyFilterConfigSharedPtr filter_config(
      std::make_shared<ProxyFilterConfig>(proto_config, cache_manager_factory,
                                          context.serverFactoryContext().clusterManager(), status));
  RETURN_IF_NOT_OK_REF(status);

  return [filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<ProxyFilter>(filter_config));
  };
}

/**
 * Static registration for the sni_dynamic_forward_proxy filter. @see RegisterFactory.
 */
REGISTER_FACTORY(SniDynamicForwardProxyNetworkFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace SniDynamicForwardProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
