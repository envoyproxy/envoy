#include "server/config/network/tcp_proxy.h"

#include <string>

#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "common/filter/tcp_proxy.h"

namespace Envoy {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb TcpProxyConfigFactory::createFilterFactory(const Json::Object& config,
                                                                  FactoryContext& context) {
  Filter::TcpProxyConfigSharedPtr filter_config(
      new Filter::TcpProxyConfig(config, context.clusterManager(), context.scope()));
  return [filter_config, &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(Network::ReadFilterSharedPtr{
        new Filter::TcpProxy(filter_config, context.clusterManager())});
  };
}

/**
 * Static registration for the tcp_proxy filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<TcpProxyConfigFactory, NamedNetworkFilterConfigFactory>
    registered_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
