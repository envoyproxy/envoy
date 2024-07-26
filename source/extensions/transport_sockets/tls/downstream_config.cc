#include "source/extensions/transport_sockets/tls/downstream_config.h"

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.validate.h"

#include "source/common/protobuf/utility.h"
#include "source/common/tls/server_context_config_impl.h"
#include "source/common/tls/server_ssl_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

absl::StatusOr<Network::DownstreamTransportSocketFactoryPtr>
DownstreamSslSocketFactory::createTransportSocketFactory(
    const Protobuf::Message& message, Server::Configuration::TransportSocketFactoryContext& context,
    const std::vector<std::string>& server_names) {
  absl::StatusOr<std::unique_ptr<ServerContextConfigImpl>> server_config_or_error =
      ServerContextConfigImpl::create(
          MessageUtil::downcastAndValidate<
              const envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext&>(
              message, context.messageValidationVisitor()),
          context, false);
  RETURN_IF_NOT_OK(server_config_or_error.status());
  return ServerSslSocketFactory::create(std::move(server_config_or_error.value()),
                                        context.sslContextManager(), context.statsScope(),
                                        server_names);
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
