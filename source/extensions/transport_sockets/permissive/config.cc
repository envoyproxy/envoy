#include "extensions/transport_sockets/permissive/config.h"

#include "common/config/utility.h"
#include "common/protobuf/utility.h"

#include "extensions/transport_sockets/permissive/permissive_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Permissive {

Network::TransportSocketFactoryPtr
UpstreamPermissiveSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& message,
    Server::Configuration::TransportSocketFactoryContext& context) {
  auto& tls_config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::UpstreamTransportSocketConfigFactory>(TransportSocketNames::get().Tls);
  auto tls_transport_socket_factory =
      tls_config_factory.createTransportSocketFactory(message, context);

  auto& raw_buffer_config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::UpstreamTransportSocketConfigFactory>(
      TransportSocketNames::get().RawBuffer);
  auto raw_buffer_transport_socket_factory =
      raw_buffer_config_factory.createTransportSocketFactory(message, context);

  return std::make_unique<PermissiveSocketFactory>(std::move(tls_transport_socket_factory),
                                                   std::move(raw_buffer_transport_socket_factory));
}

ProtobufTypes::MessagePtr UpstreamPermissiveSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::api::v2::auth::UpstreamTlsContext>();
}

REGISTER_FACTORY(UpstreamPermissiveSocketConfigFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory);

} // namespace Permissive
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy