#include "extensions/filters/network/tcp_proxy/config.h"

#include "envoy/registry/registry.h"

#include "common/tcp_proxy/tcp_proxy.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpProxy {

Network::FilterFactoryCb ConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::network::tcp_proxy::v2::TcpProxy& proto_config,
    Server::Configuration::FilterChainFactoryContext& filter_chain_factory_context) {
  ASSERT(!proto_config.stat_prefix().empty());
  if (proto_config.has_deprecated_v1()) {
    ASSERT(proto_config.deprecated_v1().routes_size() > 0);
  }

  Envoy::TcpProxy::ConfigSharedPtr filter_config(
      std::make_shared<Envoy::TcpProxy::Config>(proto_config, filter_chain_factory_context));
  return [filter_config,
          &filter_chain_factory_context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<Envoy::TcpProxy::Filter>(
        filter_config, filter_chain_factory_context.clusterManager(),
        filter_chain_factory_context.dispatcher().timeSource()));
  };
}

/**
 * Static registration for the tcp_proxy filter. @see RegisterFactory.
 */
REGISTER_FACTORY(ConfigFactory, Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace TcpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
