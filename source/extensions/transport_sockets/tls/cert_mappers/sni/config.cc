#include "source/extensions/transport_sockets/tls/cert_mappers/sni/config.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateMappers {
namespace SNI {

namespace {
class SNIMapper : public Ssl::TlsCertificateMapper {
public:
  explicit SNIMapper(const std::string& default_value) : default_value_(default_value) {}
  std::string deriveFromClientHello(const SSL_CLIENT_HELLO& ssl_client_hello) {
    absl::string_view sni = absl::NullSafeStringView(
        SSL_get_servername(ssl_client_hello.ssl, TLSEXT_NAMETYPE_host_name));
    return sni.empty() ? default_value_ : std::string(sni);
  }

private:
  const std::string default_value_;
};
} // namespace

absl::StatusOr<Ssl::TlsCertificateMapperFactory>
SNIMapperFactory::createTlsCertificateMapperFactory(
    const Protobuf::Message& proto_config,
    Server::Configuration::GenericFactoryContext& factory_context) {
  const SNIConfigProto& config = MessageUtil::downcastAndValidate<const SNIConfigProto&>(
      proto_config, factory_context.messageValidationVisitor());
  return [default_value = config.default_value()]() {
    return std::make_unique<SNIMapper>(default_value);
  };
}

REGISTER_FACTORY(SNIMapperFactory, Ssl::TlsCertificateMapperConfigFactory);

} // namespace SNI
} // namespace CertificateMappers
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
