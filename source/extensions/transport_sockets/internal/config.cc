#include "source/extensions/transport_sockets/internal/config.h"

#include "envoy/extensions/transport_sockets/internal/v3/internal_upstream.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/config/utility.h"
#include "source/extensions/transport_sockets/internal/internal.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Internal {

InternalSocketFactory::InternalSocketFactory(
    Server::Configuration::TransportSocketFactoryContext& context,
    const envoy::extensions::transport_sockets::internal::v3::InternalUpstreamTransport& config,
    Network::TransportSocketFactoryPtr&& inner_factory)
    : PassthroughFactory(std::move(inner_factory)) {
  config_ = std::make_shared<Config>(config, context.scope());
}

Network::TransportSocketPtr InternalSocketFactory::createTransportSocket(
    Network::TransportSocketOptionsConstSharedPtr options) const {
  auto inner_socket = transport_socket_factory_->createTransportSocket(options);
  if (inner_socket == nullptr) {
    return nullptr;
  }
  return std::make_unique<InternalSocket>(config_, std::move(inner_socket), options->host());
}

class InternalUpstreamConfigFactory
    : public Server::Configuration::UpstreamTransportSocketConfigFactory {
public:
  std::string name() const override { return "envoy.transport_sockets.internal_upstream"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::transport_sockets::internal::v3::InternalUpstreamTransport>();
  }
  Network::TransportSocketFactoryPtr createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override {
    const auto& outer_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::transport_sockets::internal::v3::InternalUpstreamTransport&>(
        config, context.messageValidationVisitor());
    auto& inner_config_factory = Envoy::Config::Utility::getAndCheckFactory<
        Server::Configuration::UpstreamTransportSocketConfigFactory>(
        outer_config.transport_socket());
    ProtobufTypes::MessagePtr inner_factory_config =
        Envoy::Config::Utility::translateToFactoryConfig(outer_config.transport_socket(),
                                                         context.messageValidationVisitor(),
                                                         inner_config_factory);
    auto inner_transport_factory =
        inner_config_factory.createTransportSocketFactory(*inner_factory_config, context);
    return std::make_unique<InternalSocketFactory>(context, outer_config,
                                                   std::move(inner_transport_factory));
  }
};

REGISTER_FACTORY(InternalUpstreamConfigFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory);

} // namespace Internal
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
