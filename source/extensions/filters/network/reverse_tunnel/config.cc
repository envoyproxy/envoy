#include "source/extensions/filters/network/reverse_tunnel/config.h"

#include "source/extensions/filters/network/reverse_tunnel/reverse_tunnel_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

Network::FilterFactoryCb ReverseTunnelFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config,
    Server::Configuration::FactoryContext& context) {
  auto config = std::make_shared<ReverseTunnelFilterConfig>(proto_config, context);
  // Capture scope and overload manager pointers to avoid dangling references.
  Stats::Scope* scope = &context.scope();
  Server::OverloadManager* overload_manager = &context.serverFactoryContext().overloadManager();

  return [config, scope, overload_manager](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(
        std::make_shared<ReverseTunnelFilter>(config, *scope, *overload_manager));
  };
}

/**
 * Static registration for the reverse tunnel filter.
 */
REGISTER_FACTORY(ReverseTunnelFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
