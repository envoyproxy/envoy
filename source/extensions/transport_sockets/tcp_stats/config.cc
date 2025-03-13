#include "source/extensions/transport_sockets/tcp_stats/config.h"

#include "envoy/extensions/transport_sockets/tcp_stats/v3/tcp_stats.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/config/utility.h"
#include "source/extensions/transport_sockets/tcp_stats/tcp_stats.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace TcpStats {

TcpStatsSocketFactory::TcpStatsSocketFactory(
    Server::Configuration::TransportSocketFactoryContext& context,
    const envoy::extensions::transport_sockets::tcp_stats::v3::Config& config) {
#if defined(__linux__)
  config_ = std::make_shared<Config>(config, context.statsScope());
#else
  UNREFERENCED_PARAMETER(config);
  UNREFERENCED_PARAMETER(context);
  throw EnvoyException("envoy.transport_sockets.tcp_stats is not supported on this platform.");
#endif
}

UpstreamTcpStatsSocketFactory::UpstreamTcpStatsSocketFactory(
    Server::Configuration::TransportSocketFactoryContext& context,
    const envoy::extensions::transport_sockets::tcp_stats::v3::Config& config,
    Network::UpstreamTransportSocketFactoryPtr&& inner_factory)
    : TcpStatsSocketFactory(context, config), PassthroughFactory(std::move(inner_factory)) {}

Network::TransportSocketPtr UpstreamTcpStatsSocketFactory::createTransportSocket(
    Network::TransportSocketOptionsConstSharedPtr options,
    Upstream::HostDescriptionConstSharedPtr host) const {
#if defined(__linux__)
  auto inner_socket = transport_socket_factory_->createTransportSocket(options, host);
  if (inner_socket == nullptr) {
    return nullptr;
  }
  return std::make_unique<TcpStatsSocket>(config_, std::move(inner_socket));
#else
  UNREFERENCED_PARAMETER(options);
  UNREFERENCED_PARAMETER(host);
  return nullptr;
#endif
}

DownstreamTcpStatsSocketFactory::DownstreamTcpStatsSocketFactory(
    Server::Configuration::TransportSocketFactoryContext& context,
    const envoy::extensions::transport_sockets::tcp_stats::v3::Config& config,
    Network::DownstreamTransportSocketFactoryPtr&& inner_factory)
    : TcpStatsSocketFactory(context, config),
      DownstreamPassthroughFactory(std::move(inner_factory)) {}

Network::TransportSocketPtr
DownstreamTcpStatsSocketFactory::createDownstreamTransportSocket() const {
#if defined(__linux__)
  auto inner_socket = transport_socket_factory_->createDownstreamTransportSocket();
  if (inner_socket == nullptr) {
    return nullptr;
  }
  return std::make_unique<TcpStatsSocket>(config_, std::move(inner_socket));
#else
  return nullptr;
#endif
}

class TcpStatsConfigFactory : public virtual Server::Configuration::TransportSocketConfigFactory {
public:
  std::string name() const override { return "envoy.transport_sockets.tcp_stats"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::transport_sockets::tcp_stats::v3::Config>();
  }
};

class UpstreamTcpStatsConfigFactory
    : public Server::Configuration::UpstreamTransportSocketConfigFactory,
      public TcpStatsConfigFactory {
public:
  absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr> createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override {
    const auto& outer_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::transport_sockets::tcp_stats::v3::Config&>(
        config, context.messageValidationVisitor());
    auto& inner_config_factory = Envoy::Config::Utility::getAndCheckFactory<
        Server::Configuration::UpstreamTransportSocketConfigFactory>(
        outer_config.transport_socket());
    ProtobufTypes::MessagePtr inner_factory_config =
        Envoy::Config::Utility::translateToFactoryConfig(outer_config.transport_socket(),
                                                         context.messageValidationVisitor(),
                                                         inner_config_factory);
    auto factory_or_error =
        inner_config_factory.createTransportSocketFactory(*inner_factory_config, context);
    RETURN_IF_NOT_OK_REF(factory_or_error.status());
    return std::make_unique<UpstreamTcpStatsSocketFactory>(context, outer_config,
                                                           std::move(factory_or_error.value()));
  }
};

class DownstreamTcpStatsConfigFactory
    : public Server::Configuration::DownstreamTransportSocketConfigFactory,
      public TcpStatsConfigFactory {
public:
  absl::StatusOr<Network::DownstreamTransportSocketFactoryPtr>
  createTransportSocketFactory(const Protobuf::Message& config,
                               Server::Configuration::TransportSocketFactoryContext& context,
                               const std::vector<std::string>& server_names) override {
    const auto& outer_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::transport_sockets::tcp_stats::v3::Config&>(
        config, context.messageValidationVisitor());
    auto& inner_config_factory = Envoy::Config::Utility::getAndCheckFactory<
        Server::Configuration::DownstreamTransportSocketConfigFactory>(
        outer_config.transport_socket());
    ProtobufTypes::MessagePtr inner_factory_config =
        Envoy::Config::Utility::translateToFactoryConfig(outer_config.transport_socket(),
                                                         context.messageValidationVisitor(),
                                                         inner_config_factory);
    auto factory_or_error = inner_config_factory.createTransportSocketFactory(
        *inner_factory_config, context, server_names);
    RETURN_IF_NOT_OK_REF(factory_or_error.status());
    return std::make_unique<DownstreamTcpStatsSocketFactory>(context, outer_config,
                                                             std::move(factory_or_error.value()));
  }
};

REGISTER_FACTORY(UpstreamTcpStatsConfigFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory);

REGISTER_FACTORY(DownstreamTcpStatsConfigFactory,
                 Server::Configuration::DownstreamTransportSocketConfigFactory);

} // namespace TcpStats
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
