#include "contrib/istio/filters/network/hbone/source/config.h"

#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/network/raw_buffer_socket.h"

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
    return absl::InvalidArgumentError("HBONE adapter requires HTTP CONNECT tunneling");
  }
  if (tcp.upstream_connect_mode() !=
      envoy::extensions::filters::network::tcp_proxy::v3::IMMEDIATE) {
    return absl::InvalidArgumentError(
        "HBONE adapter requires immediate upstream connection establishment");
  }
  auto shared_config = std::make_shared<TcpProxy::Config>(tcp, context);
  return Network::FilterFactoryCb([shared_config, &context](Network::FilterManager& manager) {
    manager.addReadFilter(
        std::make_shared<Filter>(shared_config, context.serverFactoryContext().clusterManager()));
  });
}

UpstreamSocketFactory::UpstreamSocketFactory(
    const SocketConfig& config, Stats::Scope& scope,
    Network::UpstreamTransportSocketFactoryPtr inner_factory)
    : PassthroughFactory(std::move(inner_factory)),
      metadata_config_(metadataConfig(config), scope) {}

Network::TransportSocketPtr
UpstreamSocketFactory::createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                                             Upstream::HostDescriptionConstSharedPtr host) const {
  auto inner = transport_socket_factory_->createTransportSocket(options, host);
  if (!inner) {
    return nullptr;
  }
  return std::make_unique<UpstreamSocket>(
      std::make_shared<ConnectState>(), host ? metadata_config_.extractMetadata(host) : nullptr,
      options ? options->downstreamSharedFilterStateObjects() : StreamInfo::FilterState::Objects{},
      std::move(inner));
}

absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr>
SocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& config,
    Server::Configuration::TransportSocketFactoryContext& context) {
  const auto& socket_config = MessageUtil::downcastAndValidate<const SocketConfig&>(
      config, context.messageValidationVisitor());
  Network::UpstreamTransportSocketFactoryPtr inner =
      std::make_unique<Network::RawBufferSocketFactory>();
  if (socket_config.has_transport_socket()) {
    auto& factory = Envoy::Config::Utility::getAndCheckFactory<
        Server::Configuration::UpstreamTransportSocketConfigFactory>(
        socket_config.transport_socket());
    auto inner_config = Envoy::Config::Utility::translateToFactoryConfig(
        socket_config.transport_socket(), context.messageValidationVisitor(), factory);
    auto result = factory.createTransportSocketFactory(*inner_config, context);
    RETURN_IF_NOT_OK_REF(result.status());
    inner = std::move(result.value());
  }
  return std::make_unique<UpstreamSocketFactory>(socket_config, context.statsScope(),
                                                 std::move(inner));
}

REGISTER_FACTORY(FilterConfigFactory, Server::Configuration::NamedNetworkFilterConfigFactory);
REGISTER_FACTORY(SocketConfigFactory, Server::Configuration::UpstreamTransportSocketConfigFactory);

} // namespace Hbone
} // namespace Istio
} // namespace Extensions
} // namespace Envoy
