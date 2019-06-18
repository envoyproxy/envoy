#include "extensions/transport_sockets/permissive/config.h"

#include "envoy/config/transport_socket/permissive/v2alpha/permissive.pb.h"
#include "envoy/config/transport_socket/permissive/v2alpha/permissive.pb.validate.h"

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

  const auto& permissive_config = MessageUtil::downcastAndValidate<
      const envoy::config::transport_socket::permissive::v2alpha::Permissive&>(message);

  // Create factory for primary transport socket.
  auto& primary_config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::UpstreamTransportSocketConfigFactory>(
      permissive_config.primary_transport_socket().name());

  ProtobufTypes::MessagePtr primary_transport_socket_factory_config =
      Config::Utility::translateToFactoryConfig(permissive_config.primary_transport_socket(),
                                                context.messageValidationVisitor(),
                                                primary_config_factory);

  auto primary_transport_socket_factory = primary_config_factory.createTransportSocketFactory(
      *primary_transport_socket_factory_config, context);

  // Create factory for secondary transport socket.
  auto& secondary_config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::UpstreamTransportSocketConfigFactory>(
      permissive_config.secondary_transport_socket().name());

  ProtobufTypes::MessagePtr secondary_transport_socket_factory_config =
      Config::Utility::translateToFactoryConfig(permissive_config.secondary_transport_socket(),
                                                context.messageValidationVisitor(),
                                                secondary_config_factory);

  auto secondary_transport_socket_factory = secondary_config_factory.createTransportSocketFactory(
      *secondary_transport_socket_factory_config, context);

  // Theoretically, falling back from unsecure to secure is unrealistic.
  ASSERT(primary_transport_socket_factory->implementsSecureTransport() ||
         (!primary_transport_socket_factory->implementsSecureTransport() &&
          !secondary_transport_socket_factory->implementsSecureTransport()));

  return std::make_unique<PermissiveSocketFactory>(std::move(primary_transport_socket_factory),
                                                   std::move(secondary_transport_socket_factory));
}

ProtobufTypes::MessagePtr UpstreamPermissiveSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::config::transport_socket::permissive::v2alpha::Permissive>();
}

REGISTER_FACTORY(UpstreamPermissiveSocketConfigFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory);

} // namespace Permissive
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy