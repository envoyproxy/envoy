#include "extensions/transport_sockets/proxy_protocol/config.h"

#include "envoy/extensions/transport_sockets/proxy_protocol/v3/upstream_proxy_protocol.pb.h"
#include "envoy/extensions/transport_sockets/proxy_protocol/v3/upstream_proxy_protocol.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/utility.h"

#include "extensions/transport_sockets/proxy_protocol/proxy_protocol.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace ProxyProtocol {

Network::TransportSocketFactoryPtr
UpstreamProxyProtocolSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& message,
    Server::Configuration::TransportSocketFactoryContext& context) {
  const auto& outer_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::transport_sockets::proxy_protocol::
                                           v3::ProxyProtocolUpstreamTransport&>(
          message, context.messageValidationVisitor());
  auto& inner_config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::UpstreamTransportSocketConfigFactory>(outer_config.transport_socket());
  ProtobufTypes::MessagePtr inner_factory_config = Config::Utility::translateToFactoryConfig(
      outer_config.transport_socket(), context.messageValidationVisitor(), inner_config_factory);
  auto inner_transport_factory =
      inner_config_factory.createTransportSocketFactory(*inner_factory_config, context);
  return std::make_unique<UpstreamProxyProtocolSocketFactory>(std::move(inner_transport_factory),
                                                              outer_config.config());
}

ProtobufTypes::MessagePtr UpstreamProxyProtocolSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::transport_sockets::proxy_protocol::v3::ProxyProtocolUpstreamTransport>();
  ;
}

REGISTER_FACTORY(UpstreamProxyProtocolSocketConfigFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory);

} // namespace ProxyProtocol
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
