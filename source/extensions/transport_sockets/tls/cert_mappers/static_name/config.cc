#include "source/extensions/transport_sockets/tls/cert_mappers/static_name/config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateMappers {
namespace StaticName {

namespace {
class StaticNameMapper : public Ssl::TlsCertificateMapper,
                         public Ssl::UpstreamTlsCertificateMapper {
public:
  explicit StaticNameMapper(const std::string& name) : name_(name) {}
  std::string deriveFromClientHello(const SSL_CLIENT_HELLO&) { return name_; }
  std::string deriveFromServerHello(const SSL&,
                                    const Network::TransportSocketOptionsConstSharedPtr&) {
    return name_;
  }

private:
  const std::string name_;
};
} // namespace

absl::StatusOr<Ssl::TlsCertificateMapperFactory>
StaticNameMapperFactory::createTlsCertificateMapperFactory(
    const Protobuf::Message& proto_config,
    Server::Configuration::GenericFactoryContext& factory_context) {
  const StaticNameConfigProto& config =
      MessageUtil::downcastAndValidate<const StaticNameConfigProto&>(
          proto_config, factory_context.messageValidationVisitor());
  return [name = config.name()]() { return std::make_unique<StaticNameMapper>(name); };
}

absl::StatusOr<Ssl::UpstreamTlsCertificateMapperFactory>
UpstreamStaticNameMapperFactory::createTlsCertificateMapperFactory(
    const Protobuf::Message& proto_config,
    Server::Configuration::GenericFactoryContext& factory_context) {
  const StaticNameConfigProto& config =
      MessageUtil::downcastAndValidate<const StaticNameConfigProto&>(
          proto_config, factory_context.messageValidationVisitor());
  return [name = config.name()]() { return std::make_unique<StaticNameMapper>(name); };
}

REGISTER_FACTORY(StaticNameMapperFactory, Ssl::TlsCertificateMapperConfigFactory);
REGISTER_FACTORY(UpstreamStaticNameMapperFactory, Ssl::UpstreamTlsCertificateMapperConfigFactory);

} // namespace StaticName
} // namespace CertificateMappers
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
