#include "source/extensions/filters/network/reverse_tunnel/config.h"

#include "source/extensions/filters/network/reverse_tunnel/reverse_tunnel_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

absl::StatusOr<Network::FilterFactoryCb>
ReverseTunnelFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config,
    Server::Configuration::FactoryContext& context) {
  auto status = validateConnLimit(proto_config);
  if (!status.ok()) {
    return status;
  }
  auto config_or_error = ReverseTunnelFilterConfig::create(proto_config, context);
  if (!config_or_error.ok()) {
    return config_or_error.status();
  }
  auto config = config_or_error.value();

  // Capture scope and overload manager pointers to avoid dangling references.
  Stats::Scope* scope = &context.scope();
  Server::OverloadManager* overload_manager = &context.serverFactoryContext().overloadManager();

  return [config, scope, overload_manager](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(
        std::make_shared<ReverseTunnelFilter>(config, *scope, *overload_manager));
  };
}

absl::Status ReverseTunnelFilterConfigFactory::validateConnLimit(
    const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config)
    const {
  if (!proto_config.enable_connection_limit()) {
    return absl::OkStatus();
  }
  const auto* acceptor = getAcceptor();
  if (acceptor == nullptr || acceptor->getExtension() == nullptr) {
    return absl::InvalidArgumentError(
        "reverse_tunnel: enable_connection_limit is set but the upstream reverse_tunnel "
        "socket interface bootstrap extension (UpstreamReverseConnectionSocketInterface) is not "
        "configured");
  }
  if (acceptor->getExtension()->maxConnectionsPerNode() == 0) {
    return absl::InvalidArgumentError(
        "reverse_tunnel: enable_connection_limit is set but max_connections_per_node is 0 on the "
        "UpstreamReverseConnectionSocketInterface bootstrap extension");
  }
  return absl::OkStatus();
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
