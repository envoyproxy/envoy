#include "contrib/istio/filters/network/hbone/source/config.h"

#include "envoy/registry/registry.h"

#include "contrib/istio/filters/network/hbone/source/hbone.h"

namespace Envoy {
namespace Extensions {
namespace Istio {
namespace Hbone {
namespace {

envoy::extensions::transport_sockets::internal_upstream::v3::InternalUpstreamTransport
metadataConfig(const SocketConfig& config) {
  envoy::extensions::transport_sockets::internal_upstream::v3::InternalUpstreamTransport result;
  *result.mutable_passthrough_metadata() = config.passthrough_metadata();
  return result;
}

} // namespace

absl::StatusOr<Network::FilterFactoryCb> FilterConfigFactory::createFilterFactoryFromProtoTyped(
    const FilterConfig& config, Server::Configuration::FactoryContext& context) {
  const auto& tcp = config.tcp_proxy();
  if (!tcp.has_tunneling_config() || tcp.tunneling_config().use_post()) {
    return absl::InvalidArgumentError("HBONE shim requires HTTP CONNECT tunneling");
  }
  if (tcp.upstream_connect_mode() !=
      envoy::extensions::filters::network::tcp_proxy::v3::IMMEDIATE) {
    return absl::InvalidArgumentError(
        "HBONE shim requires immediate upstream connection establishment");
  }
  auto shared_config = std::make_shared<TcpProxy::Config>(tcp, context);
  return Network::FilterFactoryCb([shared_config, &context](Network::FilterManager& manager) {
    manager.addReadFilter(
        std::make_shared<Filter>(shared_config, context.serverFactoryContext().clusterManager()));
  });
}

UpstreamSocketFactory::UpstreamSocketFactory(const SocketConfig& config, Stats::Scope& scope)
    : metadata_config_(metadataConfig(config), scope) {}

Network::TransportSocketPtr
UpstreamSocketFactory::createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                                             Upstream::HostDescriptionConstSharedPtr host) const {
  return std::make_unique<UpstreamSocket>(
      std::make_shared<ConnectState>(), host ? metadata_config_.extractMetadata(host) : nullptr,
      options ? options->downstreamSharedFilterStateObjects() : StreamInfo::FilterState::Objects{});
}

absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr>
SocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& config,
    Server::Configuration::TransportSocketFactoryContext& context) {
  return std::make_unique<UpstreamSocketFactory>(
      MessageUtil::downcastAndValidate<const SocketConfig&>(config,
                                                            context.messageValidationVisitor()),
      context.statsScope());
}

REGISTER_FACTORY(FilterConfigFactory, Server::Configuration::NamedNetworkFilterConfigFactory);
REGISTER_FACTORY(SocketConfigFactory, Server::Configuration::UpstreamTransportSocketConfigFactory);

} // namespace Hbone
} // namespace Istio
} // namespace Extensions
} // namespace Envoy
