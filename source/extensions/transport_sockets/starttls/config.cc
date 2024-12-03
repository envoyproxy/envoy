#include "source/extensions/transport_sockets/starttls/config.h"

#include "source/extensions/transport_sockets/starttls/starttls_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace StartTls {

absl::StatusOr<Network::DownstreamTransportSocketFactoryPtr>
DownstreamStartTlsSocketFactory::createTransportSocketFactory(
    const Protobuf::Message& message, Server::Configuration::TransportSocketFactoryContext& context,
    const std::vector<std::string>& server_names) {
  const auto& outer_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::transport_sockets::starttls::v3::StartTlsConfig&>(
      message, context.messageValidationVisitor());

  auto& raw_socket_config_factory = rawSocketConfigFactory();
  auto& tls_socket_config_factory = tlsSocketConfigFactory();

  auto raw_or_error = raw_socket_config_factory.createTransportSocketFactory(
      outer_config.cleartext_socket_config(), context, server_names);
  RETURN_IF_NOT_OK_REF(raw_or_error.status());

  auto factory_or_error = tls_socket_config_factory.createTransportSocketFactory(
      outer_config.tls_socket_config(), context, server_names);
  RETURN_IF_NOT_OK_REF(factory_or_error.status());

  return std::make_unique<StartTlsDownstreamSocketFactory>(std::move(raw_or_error.value()),
                                                           std::move(factory_or_error.value()));
}

absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr>
UpstreamStartTlsSocketFactory::createTransportSocketFactory(
    const Protobuf::Message& message,
    Server::Configuration::TransportSocketFactoryContext& context) {

  const auto& outer_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::transport_sockets::starttls::v3::UpstreamStartTlsConfig&>(
      message, context.messageValidationVisitor());
  auto& raw_socket_config_factory = rawSocketConfigFactory();
  auto& tls_socket_config_factory = tlsSocketConfigFactory();

  auto raw_or_error = raw_socket_config_factory.createTransportSocketFactory(
      outer_config.cleartext_socket_config(), context);
  RETURN_IF_NOT_OK_REF(raw_or_error.status());

  auto factory_or_error = tls_socket_config_factory.createTransportSocketFactory(
      outer_config.tls_socket_config(), context);
  RETURN_IF_NOT_OK_REF(factory_or_error.status());

  return std::make_unique<StartTlsSocketFactory>(std::move(raw_or_error.value()),
                                                 std::move(factory_or_error.value()));
}

LEGACY_REGISTER_FACTORY(DownstreamStartTlsSocketFactory,
                        Server::Configuration::DownstreamTransportSocketConfigFactory, "starttls");

LEGACY_REGISTER_FACTORY(UpstreamStartTlsSocketFactory,
                        Server::Configuration::UpstreamTransportSocketConfigFactory, "starttls");

} // namespace StartTls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
