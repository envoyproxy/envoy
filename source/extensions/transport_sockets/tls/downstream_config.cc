#include "source/extensions/transport_sockets/tls/downstream_config.h"

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.validate.h"

#include "source/common/protobuf/utility.h"
#include "source/common/tls/context_config_impl.h"
#include "source/common/tls/ssl_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

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
      std::move(server_config), context.sslContextManager(), context.statsScope(), server_names);
}

ProtobufTypes::MessagePtr DownstreamSslSocketFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext>();
}

LEGACY_REGISTER_FACTORY(DownstreamSslSocketFactory,
                        Server::Configuration::DownstreamTransportSocketConfigFactory, "tls");

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
