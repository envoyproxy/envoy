#include "envoy/extensions/transport_sockets/tls/cert_mappers/sni/v3/config.pb.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/status_utility.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateMappers {
namespace SNI {
namespace {

using ::testing::NiceMock;

TEST(SNIMapper, DerivationSNI) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> factory_context;
  Ssl::TlsCertificateMapperConfigFactory& mapper_factory =
      Config::Utility::getAndCheckFactoryByName<Ssl::TlsCertificateMapperConfigFactory>(
          "envoy.tls.certificate_mappers.sni");
  envoy::extensions::transport_sockets::tls::cert_mappers::sni::v3::SNI config;
  TestUtility::loadFromYaml(R"EOF(
    default_value: "*"
  )EOF",
                            config);
  auto mapper_status = mapper_factory.createTlsCertificateMapperFactory(config, factory_context);
  ASSERT_OK(mapper_status);
  auto mapper = mapper_status.value()();
  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));
  bssl::UniquePtr<SSL> ssl(SSL_new(ctx.get()));
  SSL_set_tlsext_host_name(ssl.get(), "example.com");
  SSL_CLIENT_HELLO ssl_client_hello;
  ssl_client_hello.ssl = ssl.get();
  EXPECT_EQ("example.com", mapper->deriveFromClientHello(ssl_client_hello));
}

TEST(SNIMapper, DerivationECDSA) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> factory_context;
  Ssl::TlsCertificateMapperConfigFactory& mapper_factory =
      Config::Utility::getAndCheckFactoryByName<Ssl::TlsCertificateMapperConfigFactory>(
          "envoy.tls.certificate_mappers.sni");
  envoy::extensions::transport_sockets::tls::cert_mappers::sni::v3::SNI config;
  TestUtility::loadFromYaml(R"EOF(
    default_value: "*"
    include_signature_algorithm: true
  )EOF",
                            config);
  auto mapper_status = mapper_factory.createTlsCertificateMapperFactory(config, factory_context);
  ASSERT_OK(mapper_status);
  auto mapper = mapper_status.value()();
  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));
  bssl::UniquePtr<SSL> ssl(SSL_new(ctx.get()));
  SSL_set_tlsext_host_name(ssl.get(), "example.com");
  SSL_CLIENT_HELLO ssl_client_hello;
  ssl_client_hello.ssl = ssl.get();
  // Simulate ECDSA support in client hello.
  constexpr uint8_t ecdsa_cipher_suite[] = {(TLS1_3_CK_AES_256_GCM_SHA384 >> 8) & 0xFF,
                                            TLS1_3_CK_AES_256_GCM_SHA384 & 0xFF};
  ssl_client_hello.cipher_suites = ecdsa_cipher_suite;
  ssl_client_hello.cipher_suites_len = sizeof(ecdsa_cipher_suite);
  EXPECT_EQ("example.com/ecdsa", mapper->deriveFromClientHello(ssl_client_hello));
}

TEST(SNIMapper, DerivationRSA) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> factory_context;
  Ssl::TlsCertificateMapperConfigFactory& mapper_factory =
      Config::Utility::getAndCheckFactoryByName<Ssl::TlsCertificateMapperConfigFactory>(
          "envoy.tls.certificate_mappers.sni");
  envoy::extensions::transport_sockets::tls::cert_mappers::sni::v3::SNI config;
  TestUtility::loadFromYaml(R"EOF(
    default_value: "*"
    include_signature_algorithm: true
  )EOF",
                            config);
  auto mapper_status = mapper_factory.createTlsCertificateMapperFactory(config, factory_context);
  ASSERT_OK(mapper_status);
  auto mapper = mapper_status.value()();
  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));
  bssl::UniquePtr<SSL> ssl(SSL_new(ctx.get()));
  SSL_set_tlsext_host_name(ssl.get(), "example.com");
  SSL_CLIENT_HELLO ssl_client_hello;
  ssl_client_hello.ssl = ssl.get();
  // Simulate RSA support in client hello.
  constexpr uint8_t ecdsa_cipher_suite[] = {(TLS1_CK_DH_RSA_WITH_AES_256_SHA256 >> 8) & 0xFF,
                                            TLS1_CK_DH_RSA_WITH_AES_256_SHA256 & 0xFF};
  ssl_client_hello.cipher_suites = ecdsa_cipher_suite;
  ssl_client_hello.cipher_suites_len = sizeof(ecdsa_cipher_suite);
  EXPECT_EQ("example.com/rsa", mapper->deriveFromClientHello(ssl_client_hello));
}

} // namespace
} // namespace SNI
} // namespace CertificateMappers
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
