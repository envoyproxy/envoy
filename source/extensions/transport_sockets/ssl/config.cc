#include "extensions/transport_sockets/ssl/config.h"

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/api/v2/auth/cert.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"
#include "common/ssl/context_config_impl.h"
#include "common/ssl/ssl_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace SslTransport {

Network::TransportSocketFactoryPtr UpstreamSslSocketFactory::createTransportSocketFactory(
    const Protobuf::Message& message,
    Server::Configuration::TransportSocketFactoryContext& context) {
  std::unique_ptr<Ssl::ClientContextConfigImpl> upstream_config =
      std::make_unique<Ssl::ClientContextConfigImpl>(
          MessageUtil::downcastAndValidate<const envoy::api::v2::auth::UpstreamTlsContext&>(
              message),
          context.secretManager(), context.dynamicTlsCertificateSecretProviderFactory());
  return std::make_unique<Ssl::ClientSslSocketFactory>(
      std::move(upstream_config), context.sslContextManager(), context.statsScope());
}

ProtobufTypes::MessagePtr UpstreamSslSocketFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::api::v2::auth::UpstreamTlsContext>();
}

static Registry::RegisterFactory<UpstreamSslSocketFactory,
                                 Server::Configuration::UpstreamTransportSocketConfigFactory>
    upstream_registered_;

Network::TransportSocketFactoryPtr DownstreamSslSocketFactory::createTransportSocketFactory(
    const Protobuf::Message& message, Server::Configuration::TransportSocketFactoryContext& context,
    const std::vector<std::string>& server_names) {
  std::unique_ptr<Ssl::ServerContextConfigImpl> downstream_config =
      std::make_unique<Ssl::ServerContextConfigImpl>(
          MessageUtil::downcastAndValidate<const envoy::api::v2::auth::DownstreamTlsContext&>(
              message),
          context.secretManager(), context.dynamicTlsCertificateSecretProviderFactory());
  return std::make_unique<Ssl::ServerSslSocketFactory>(std::move(downstream_config),
                                                       context.sslContextManager(),
                                                       context.statsScope(), server_names);
}

ProtobufTypes::MessagePtr DownstreamSslSocketFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::api::v2::auth::DownstreamTlsContext>();
}

static Registry::RegisterFactory<DownstreamSslSocketFactory,
                                 Server::Configuration::DownstreamTransportSocketConfigFactory>
    downstream_registered_;

} // namespace SslTransport
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
