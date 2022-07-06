#include "source/extensions/transport_sockets/http_11_proxy/config.h"

#include "envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.pb.h"
#include "envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/extensions/transport_sockets/http_11_proxy/connect.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Http11Connect {

Network::UpstreamTransportSocketFactoryPtr
UpstreamHttp11ConnectSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& message,
    Server::Configuration::TransportSocketFactoryContext& context) {
  const auto& outer_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::transport_sockets::http_11_proxy::v3::Http11ProxyUpstreamTransport&>(
      message, context.messageValidationVisitor());
  auto& inner_config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::UpstreamTransportSocketConfigFactory>(outer_config.transport_socket());
  ProtobufTypes::MessagePtr inner_factory_config = Config::Utility::translateToFactoryConfig(
      outer_config.transport_socket(), context.messageValidationVisitor(), inner_config_factory);
  auto inner_transport_factory =
      inner_config_factory.createTransportSocketFactory(*inner_factory_config, context);
  return std::make_unique<UpstreamHttp11ConnectSocketFactory>(std::move(inner_transport_factory));
}

ProtobufTypes::MessagePtr UpstreamHttp11ConnectSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::transport_sockets::http_11_proxy::v3::Http11ProxyUpstreamTransport>();
}

REGISTER_FACTORY(UpstreamHttp11ConnectSocketConfigFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory);

} // namespace Http11Connect
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
