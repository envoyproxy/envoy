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
  auto status = validateConfig(proto_config);
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

absl::Status ReverseTunnelFilterConfigFactory::validateConfig(
    const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config)
    const {
  // The filter registers every accepted tunnel with the upstream reverse tunnel acceptor, so its
  // bootstrap extension must be configured. Without it the handshake would answer 200 and drop the
  // socket, leaving the initiator with a tunnel nobody can consume.
  const auto* acceptor = getAcceptor();
  if (acceptor == nullptr || acceptor->getExtension() == nullptr) {
    return absl::InvalidArgumentError(
        "reverse_tunnel: the upstream reverse_tunnel socket interface bootstrap extension "
        "(UpstreamReverseConnectionSocketInterface) is not configured");
  }
  // When the per-node connection cap is enforced it must be set on the extension.
  if (proto_config.enable_connection_limit() &&
      acceptor->getExtension()->maxConnectionsPerNode() == 0) {
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
