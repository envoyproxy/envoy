#include "source/extensions/transport_sockets/tls/upstream_config.h"

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.validate.h"

#include "source/common/protobuf/utility.h"
#include "source/common/tls/client_ssl_socket.h"
#include "source/common/tls/context_config_impl.h"

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
  return std::make_unique<ClientSslSocketFactory>(
      std::move(client_config), context.sslContextManager(), context.statsScope());
}

ProtobufTypes::MessagePtr UpstreamSslSocketFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext>();
}

LEGACY_REGISTER_FACTORY(UpstreamSslSocketFactory,
                        Server::Configuration::UpstreamTransportSocketConfigFactory, "tls");

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
