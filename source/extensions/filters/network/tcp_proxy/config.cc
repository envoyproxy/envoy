#include "source/extensions/filters/network/tcp_proxy/config.h"

#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/tcp_proxy/tcp_proxy.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpProxy {

Network::FilterFactoryCb ConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy& proto_config,
    const Network::NetworkFilterMatcherSharedPtr& network_filter_matcher,
    Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.stat_prefix().empty());

  Envoy::TcpProxy::ConfigSharedPtr filter_config(
      std::make_shared<Envoy::TcpProxy::Config>(proto_config, context));
  return [network_filter_matcher, filter_config,
          &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(
        network_filter_matcher,
        std::make_shared<Envoy::TcpProxy::Filter>(filter_config, context.clusterManager()));
  };
}

/**
 * Static registration for the tcp_proxy filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(ConfigFactory, Server::Configuration::NamedNetworkFilterConfigFactory,
                        "envoy.tcp_proxy");

} // namespace TcpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
