#include "source/common/quic/quic_transport_socket_factory.h"

#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.validate.h"

#include "source/extensions/transport_sockets/tls/context_config_impl.h"

namespace Envoy {
namespace Quic {

Network::TransportSocketFactoryPtr
QuicServerTransportSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& config, Server::Configuration::TransportSocketFactoryContext& context,
    const std::vector<std::string>& /*server_names*/) {
  auto quic_transport = MessageUtil::downcastAndValidate<
      const envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport&>(
      config, context.messageValidationVisitor());
  auto server_config = std::make_unique<Extensions::TransportSockets::Tls::ServerContextConfigImpl>(
      quic_transport.downstream_tls_context(), context);
  auto factory =
      std::make_unique<QuicServerTransportSocketFactory>(context.scope(), std::move(server_config));
  factory->initialize();
  return factory;
}

ProtobufTypes::MessagePtr QuicServerTransportSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport>();
}

Network::TransportSocketFactoryPtr
QuicClientTransportSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& config,
    Server::Configuration::TransportSocketFactoryContext& context) {
  auto quic_transport = MessageUtil::downcastAndValidate<
      const envoy::extensions::transport_sockets::quic::v3::QuicUpstreamTransport&>(
      config, context.messageValidationVisitor());
  auto client_config = std::make_unique<Extensions::TransportSockets::Tls::ClientContextConfigImpl>(
      quic_transport.upstream_tls_context(), context);
  auto factory =
      std::make_unique<QuicClientTransportSocketFactory>(std::move(client_config), context);
  factory->initialize();
  return factory;
}

QuicClientTransportSocketFactory::QuicClientTransportSocketFactory(
    Ssl::ClientContextConfigPtr config,
    Server::Configuration::TransportSocketFactoryContext& factory_context)
    : QuicTransportSocketFactoryBase(factory_context.scope(), "client"),
      fallback_factory_(std::make_unique<Extensions::TransportSockets::Tls::ClientSslSocketFactory>(
          std::move(config), factory_context.sslContextManager(), factory_context.scope())) {}

ProtobufTypes::MessagePtr QuicClientTransportSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::transport_sockets::quic::v3::QuicUpstreamTransport>();
}

REGISTER_FACTORY(QuicServerTransportSocketConfigFactory,
                 Server::Configuration::DownstreamTransportSocketConfigFactory);

REGISTER_FACTORY(QuicClientTransportSocketConfigFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory);

} // namespace Quic
} // namespace Envoy
