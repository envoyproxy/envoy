#include "extensions/transport_sockets/starttls/config.h"

#include "envoy/extensions/transport_sockets/starttls/v3/starttls.pb.h"
#include "envoy/extensions/transport_sockets/starttls/v3/starttls.pb.validate.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "common/config/utility.h"

#include "extensions/transport_sockets/starttls/starttls_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace StartTls {

Network::TransportSocketFactoryPtr DownstreamStartTlsSocketFactory::createTransportSocketFactory(
    const Protobuf::Message& message, Server::Configuration::TransportSocketFactoryContext& context,
    const std::vector<std::string>& server_names) {
  const auto& outer_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::transport_sockets::starttls::v3::StartTlsConfig&>(
      message, context.messageValidationVisitor());

  auto& raw_socket_config_factory = Config::Utility::getAndCheckFactoryByName<
      Server::Configuration::DownstreamTransportSocketConfigFactory>(
      TransportSocketNames::get().RawBuffer);

  Network::TransportSocketFactoryPtr raw_socket_factory =
      raw_socket_config_factory.createTransportSocketFactory(outer_config.cleartext_socket_config(),
                                                             context, server_names);

  auto& tls_socket_config_factory = Config::Utility::getAndCheckFactoryByName<
      Server::Configuration::DownstreamTransportSocketConfigFactory>(
      TransportSocketNames::get().Tls);

  Network::TransportSocketFactoryPtr tls_socket_factory =
      tls_socket_config_factory.createTransportSocketFactory(outer_config.tls_socket_config(),
                                                             context, server_names);

  return std::make_unique<ServerStartTlsSocketFactory>(outer_config, std::move(raw_socket_factory),
                                                       std::move(tls_socket_factory));
}

ProtobufTypes::MessagePtr DownstreamStartTlsSocketFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::transport_sockets::starttls::v3::StartTlsConfig>();
}

REGISTER_FACTORY(DownstreamStartTlsSocketFactory,
                 Server::Configuration::DownstreamTransportSocketConfigFactory){"starttls"};

} // namespace StartTls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
