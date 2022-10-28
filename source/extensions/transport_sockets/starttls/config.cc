#include "source/extensions/transport_sockets/starttls/config.h"

#include "source/extensions/transport_sockets/starttls/starttls_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace StartTls {

Network::DownstreamTransportSocketFactoryPtr
DownstreamStartTlsSocketFactory::createTransportSocketFactory(
    const Protobuf::Message& message, Server::Configuration::TransportSocketFactoryContext& context,
    const std::vector<std::string>& server_names) {
  const auto& outer_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::transport_sockets::starttls::v3::StartTlsConfig&>(
      message, context.messageValidationVisitor());

  auto& raw_socket_config_factory = rawSocketConfigFactory();
  auto& tls_socket_config_factory = tlsSocketConfigFactory();

  Network::DownstreamTransportSocketFactoryPtr raw_socket_factory =
      raw_socket_config_factory.createTransportSocketFactory(outer_config.cleartext_socket_config(),
                                                             context, server_names);

  Network::DownstreamTransportSocketFactoryPtr tls_socket_factory =
      tls_socket_config_factory.createTransportSocketFactory(outer_config.tls_socket_config(),
                                                             context, server_names);

  return std::make_unique<StartTlsDownstreamSocketFactory>(std::move(raw_socket_factory),
                                                           std::move(tls_socket_factory));
}

Network::UpstreamTransportSocketFactoryPtr
UpstreamStartTlsSocketFactory::createTransportSocketFactory(
    const Protobuf::Message& message,
    Server::Configuration::TransportSocketFactoryContext& context) {

  const auto& outer_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::transport_sockets::starttls::v3::UpstreamStartTlsConfig&>(
      message, context.messageValidationVisitor());
  auto& raw_socket_config_factory = rawSocketConfigFactory();
  auto& tls_socket_config_factory = tlsSocketConfigFactory();

  Network::UpstreamTransportSocketFactoryPtr raw_socket_factory =
      raw_socket_config_factory.createTransportSocketFactory(outer_config.cleartext_socket_config(),
                                                             context);

  Network::UpstreamTransportSocketFactoryPtr tls_socket_factory =
      tls_socket_config_factory.createTransportSocketFactory(outer_config.tls_socket_config(),
                                                             context);

  return std::make_unique<StartTlsSocketFactory>(std::move(raw_socket_factory),
                                                 std::move(tls_socket_factory));
}

REGISTER_FACTORY(DownstreamStartTlsSocketFactory,
                 Server::Configuration::DownstreamTransportSocketConfigFactory){"starttls"};

REGISTER_FACTORY(UpstreamStartTlsSocketFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory){"starttls"};

} // namespace StartTls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
