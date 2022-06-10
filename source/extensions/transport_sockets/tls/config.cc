#include "source/extensions/transport_sockets/tls/config.h"

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.validate.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/transport_sockets/tls/context_config_impl.h"
#include "source/extensions/transport_sockets/tls/ssl_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

Network::UpstreamTransportSocketFactoryPtr UpstreamSslSocketFactory::createTransportSocketFactory(
    const Protobuf::Message& message,
    Server::Configuration::TransportSocketFactoryContext& context) {
  auto client_config = std::make_unique<ClientContextConfigImpl>(
      MessageUtil::downcastAndValidate<
          const envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext&>(
          message, context.messageValidationVisitor()),
      context);
  return std::make_unique<ClientSslSocketFactory>(std::move(client_config),
                                                  context.sslContextManager(), context.scope());
}

ProtobufTypes::MessagePtr UpstreamSslSocketFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext>();
}

REGISTER_FACTORY(UpstreamSslSocketFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory){"tls"};

Network::DownstreamTransportSocketFactoryPtr
DownstreamSslSocketFactory::createTransportSocketFactory(
    const Protobuf::Message& message, Server::Configuration::TransportSocketFactoryContext& context,
    const std::vector<std::string>& server_names) {
  auto server_config = std::make_unique<ServerContextConfigImpl>(
      MessageUtil::downcastAndValidate<
          const envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext&>(
          message, context.messageValidationVisitor()),
      context);
  return std::make_unique<ServerSslSocketFactory>(
      std::move(server_config), context.sslContextManager(), context.scope(), server_names);
}

ProtobufTypes::MessagePtr DownstreamSslSocketFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext>();
}

REGISTER_FACTORY(DownstreamSslSocketFactory,
                 Server::Configuration::DownstreamTransportSocketConfigFactory){"tls"};

Ssl::ContextManagerPtr SslContextManagerFactory::createContextManager(TimeSource& time_source) {
  return std::make_unique<ContextManagerImpl>(time_source);
}

static Envoy::Registry::RegisterInternalFactory<SslContextManagerFactory,
                                                Ssl::ContextManagerFactory>
    ssl_manager_registered;

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
