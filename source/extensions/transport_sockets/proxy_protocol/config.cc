#include "extensions/transport_sockets/proxy_protocol/config.h"

#include "envoy/extensions/transport_sockets/proxy_protocol/v3/proxy_protocol.pb.h"
#include "envoy/extensions/transport_sockets/proxy_protocol/v3/proxy_protocol.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/utility.h"

#include "extensions/transport_sockets/proxy_protocol/proxy_protocol.h"

// #include "common/config/utility.h"
// #include "common/network/raw_buffer_socket.h"
// #include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace ProxyProtocol {

Network::TransportSocketFactoryPtr ProxyProtocolSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& message,
    Server::Configuration::TransportSocketFactoryContext& context) {
  const auto& outer_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::transport_sockets::proxy_protocol::v3::ProxyProtocol&>(
      message, context.messageValidationVisitor());
  auto& inner_config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::UpstreamTransportSocketConfigFactory>(outer_config.transport_socket());
  ProtobufTypes::MessagePtr inner_factory_config = Config::Utility::translateToFactoryConfig(
      outer_config.transport_socket(), context.messageValidationVisitor(), inner_config_factory);
  auto inner_transport_factory =
      inner_config_factory.createTransportSocketFactory(*inner_factory_config, context);
  return std::make_unique<ProxyProtocolSocketFactory>(std::move(inner_transport_factory),
                                                      outer_config.version());
}

ProtobufTypes::MessagePtr ProxyProtocolSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::transport_sockets::proxy_protocol::v3::ProxyProtocol>();
  ;
}

REGISTER_FACTORY(ProxyProtocolSocketConfigFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory);

} // namespace ProxyProtocol
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
