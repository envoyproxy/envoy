#include "source/extensions/transport_sockets/tls/cert_mappers/static_name/config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateMappers {
namespace StaticName {

absl::StatusOr<Ssl::TlsCertificateMapperFactory>
StaticNameMapperFactory::createTlsCertificateMapperFactory(
    const Protobuf::Message& proto_config,
    Server::Configuration::GenericFactoryContext& factory_context) {
  const StaticNameConfigProto& config =
      MessageUtil::downcastAndValidate<const StaticNameConfigProto&>(
          proto_config, factory_context.messageValidationVisitor());
  return [name = config.name()]() { return [=](const SSL_CLIENT_HELLO&) { return name; }; };
}

REGISTER_FACTORY(StaticNameMapperFactory, Ssl::TlsCertificateMapperConfigFactory);

} // namespace StaticName
} // namespace CertificateMappers
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
