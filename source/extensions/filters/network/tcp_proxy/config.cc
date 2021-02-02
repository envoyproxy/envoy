#include "extensions/filters/network/tcp_proxy/config.h"

#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/tcp_proxy/tcp_proxy.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpProxy {

Network::FilterFactoryCb ConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy& proto_config,
    Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.stat_prefix().empty());
  auto _ = Envoy::Router::HeaderParser::configure(proto_config.tunneling_config().headers_to_add());

  if (proto_config.has_hidden_envoy_deprecated_deprecated_v1()) {
    ASSERT(proto_config.hidden_envoy_deprecated_deprecated_v1().routes_size() > 0);
  }

  Envoy::TcpProxy::ConfigSharedPtr filter_config(
      std::make_shared<Envoy::TcpProxy::Config>(proto_config, context));
  return [filter_config, &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(
        std::make_shared<Envoy::TcpProxy::Filter>(filter_config, context.clusterManager()));
  };
}

/**
 * Static registration for the tcp_proxy filter. @see RegisterFactory.
 */
REGISTER_FACTORY(ConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory){"envoy.tcp_proxy"};

} // namespace TcpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
