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
  explicit SNIMapper(const std::string& default_value, const bool include_signature_algorithm)
      : default_value_(default_value), include_signature_algorithm_(include_signature_algorithm) {}
  std::string deriveFromClientHello(const SSL_CLIENT_HELLO& ssl_client_hello) {
    absl::string_view sni = absl::NullSafeStringView(
        SSL_get_servername(ssl_client_hello.ssl, TLSEXT_NAMETYPE_host_name));
    if (include_signature_algorithm_) {
      return deriveWithSignatureAlgorithm(ssl_client_hello, sni);
    }
    return sni.empty() ? default_value_ : std::string(sni);
  }

  std::string deriveWithSignatureAlgorithm(const SSL_CLIENT_HELLO& ssl_client_hello,
                                           const absl::string_view sni) const {
    for (size_t i = 0; i < ssl_client_hello.cipher_suites_len; i += 2) {
      uint16_t cipher_suite =
          (ssl_client_hello.cipher_suites[i] << 8) | ssl_client_hello.cipher_suites[i + 1];
      const SSL_CIPHER* cipher = SSL_get_cipher_by_value(cipher_suite);
      if (cipher == nullptr) {
        continue;
      }
      const int sig_alg = SSL_CIPHER_get_auth_nid(cipher);
      // TLS 1.3 signature algorithms will return NID_auth_any.
      if (sig_alg == NID_auth_ecdsa || sig_alg == NID_auth_any) {
        // return SNI with ECDSA suffix early if ECDSA support is detected
        return (sni.empty() ? default_value_ : std::string(sni)) + "/ecdsa";
      }
    }
    return (sni.empty() ? default_value_ : std::string(sni)) + "/rsa";
  }

private:
  const std::string default_value_;
  const bool include_signature_algorithm_;
};
} // namespace

absl::StatusOr<Ssl::TlsCertificateMapperFactory>
SNIMapperFactory::createTlsCertificateMapperFactory(
    const Protobuf::Message& proto_config,
    Server::Configuration::GenericFactoryContext& factory_context) {
  const SNIConfigProto& config = MessageUtil::downcastAndValidate<const SNIConfigProto&>(
      proto_config, factory_context.messageValidationVisitor());
  return [default_value = config.default_value(),
          include_signature_algorithm = config.include_signature_algorithm()]() {
    return std::make_unique<SNIMapper>(default_value, include_signature_algorithm);
  };
}

REGISTER_FACTORY(SNIMapperFactory, Ssl::TlsCertificateMapperConfigFactory);

} // namespace SNI
} // namespace CertificateMappers
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
