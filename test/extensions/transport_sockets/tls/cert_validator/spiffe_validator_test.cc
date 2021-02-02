#include <memory>
#include <string>
#include <vector>

#include "extensions/transport_sockets/tls/cert_validator/spiffe_validator.h"

#include "test/extensions/transport_sockets/tls/cert_validator/util.h"
#include "test/extensions/transport_sockets/tls/ssl_test_utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

using SPIFFEValidatorPtr = std::unique_ptr<SPIFFEValidator>;
using X509StoreContextPtr = CSmartPtr<X509_STORE_CTX, X509_STORE_CTX_free>;
using SSLContextPtr = CSmartPtr<SSL_CTX, SSL_CTX_free>;

class TestSPIFFEValidator : public SPIFFEValidator, public testing::Test {
public:
  void initialize(std::string yaml) {
    envoy::config::core::v3::TypedExtensionConfig typed_conf;
    TestUtility::loadFromYaml(yaml, typed_conf);
    TestCertificateValidationContextConfig config(typed_conf);
    validator_ = std::make_unique<SPIFFEValidator>(&config);
  };

  SPIFFEValidator& validator() { return *validator_; };

private:
  SPIFFEValidatorPtr validator_;
};

TEST_F(TestSPIFFEValidator, Constructor) {
  EXPECT_THROW_WITH_MESSAGE(initialize(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_bundles: {}
  )EOF")),
                            EnvoyException,
                            "SPIFFE cert validator requires at least one trusted CA");

  initialize(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_bundles:
    example.com:
      filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
    k8s-west.example.com:
      filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/keyusage_crl_sign_cert.pem"
  )EOF"));

  EXPECT_EQ(2, validator().trustBundleStores().size());
}

TEST(SPIFFEValidator, TestExtractTrustDomain) {
  EXPECT_EQ("", SPIFFEValidator::extractTrustDomain("abc.com/"));
  EXPECT_EQ("abc.com", SPIFFEValidator::extractTrustDomain("spiffe://abc.com/"));
  EXPECT_EQ("dev.envoy.com",
            SPIFFEValidator::extractTrustDomain("spiffe://dev.envoy.com/workload1"));
  EXPECT_EQ("k8s-west.example.com", SPIFFEValidator::extractTrustDomain(
                                        "spiffe://k8s-west.example.com/ns/staging/sa/default"));
}

TEST(SPIFFEValidator, TestCertificatePrecheck) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      // basicConstraints: CA:True,
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"));
  EXPECT_EQ(0, SPIFFEValidator::certificatePrecheck(cert.get()));

  cert = readCertFromFile(TestEnvironment::substitute(
      // basicConstraints CA:False, keyUsage has keyCertSign
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/keyusage_cert_sign_cert.pem"));
  EXPECT_EQ(0, SPIFFEValidator::certificatePrecheck(cert.get()));

  cert = readCertFromFile(TestEnvironment::substitute(
      // basicConstraints CA:False, keyUsage has cRLSign
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/keyusage_crl_sign_cert.pem"));
  EXPECT_EQ(0, SPIFFEValidator::certificatePrecheck(cert.get()));

  cert = readCertFromFile(TestEnvironment::substitute(
      // basicConstraints CA:False, keyUsage does not have keyCertSign and cRLSign
      // should be considered valid (i.e. return 1)
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/extensions_cert.pem"));
  EXPECT_EQ(1, SPIFFEValidator::certificatePrecheck(cert.get()));
}

TEST(SPIFFEValidator, TestInitializeSslContexts) {
  auto validator = SPIFFEValidator{};
  EXPECT_EQ(SSL_VERIFY_PEER, validator.initializeSslContexts({}, false));
}

TEST(SPIFFEValidator, TestGetTrustBundleStore) {
  auto validator = SPIFFEValidator{};
  // no san
  auto cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/extensions_cert.pem"));
  EXPECT_FALSE(validator.getTrustBundleStore(cert.get()));

  // spiffe san
  cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"));

  // trust bundle not provided
  EXPECT_FALSE(validator.getTrustBundleStore(cert.get()));

  // trust bundle provided
  validator.trustBundleStores().emplace("lyft.com", X509StorePtr(X509_STORE_new()));
  EXPECT_TRUE(validator.getTrustBundleStore(cert.get()));
}

TEST_F(TestSPIFFEValidator, TestDoVerifyCertChainSingleTrustDomain) {
  initialize(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_bundles:
    lyft.com:
      filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
  )EOF"));

  X509StorePtr ssl_ctx = X509_STORE_new();

  // trust domain match so should be accepted
  auto cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"));
  X509StoreContextPtr store_ctx = X509_STORE_CTX_new();
  EXPECT_TRUE(X509_STORE_CTX_init(store_ctx.get(), ssl_ctx.get(), cert.get(), nullptr));
  EXPECT_TRUE(validator().doVerifyCertChain(store_ctx.get(), nullptr, *cert, nullptr));

  // different trust domain so should be rejected
  cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/spiffe_san_cert.pem"));

  store_ctx = X509_STORE_CTX_new();
  EXPECT_TRUE(X509_STORE_CTX_init(store_ctx.get(), ssl_ctx.get(), cert.get(), nullptr));
  EXPECT_FALSE(validator().doVerifyCertChain(store_ctx.get(), nullptr, *cert, nullptr));

  // does not have san
  cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/extensions_cert.pem"));

  store_ctx = X509_STORE_CTX_new();
  EXPECT_TRUE(X509_STORE_CTX_init(store_ctx.get(), ssl_ctx.get(), cert.get(), nullptr));
  EXPECT_FALSE(validator().doVerifyCertChain(store_ctx.get(), nullptr, *cert, nullptr));
}

TEST_F(TestSPIFFEValidator, TestDoVerifyCertChainMultipleTrustDomain) {
  initialize(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_bundles:
    lyft.com:
      filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
    example.com:
      filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
  )EOF"));

  X509StorePtr ssl_ctx = X509_STORE_new();

  // trust domain match so should be accepted
  auto cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"));
  X509StoreContextPtr store_ctx = X509_STORE_CTX_new();
  EXPECT_TRUE(X509_STORE_CTX_init(store_ctx.get(), ssl_ctx.get(), cert.get(), nullptr));
  EXPECT_TRUE(validator().doVerifyCertChain(store_ctx.get(), nullptr, *cert, nullptr));

  cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/spiffe_san_cert.pem"));

  store_ctx = X509_STORE_CTX_new();
  EXPECT_TRUE(X509_STORE_CTX_init(store_ctx.get(), ssl_ctx.get(), cert.get(), nullptr));
  EXPECT_TRUE(validator().doVerifyCertChain(store_ctx.get(), nullptr, *cert, nullptr));

  // does not have san
  cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/extensions_cert.pem"));

  store_ctx = X509_STORE_CTX_new();
  EXPECT_TRUE(X509_STORE_CTX_init(store_ctx.get(), ssl_ctx.get(), cert.get(), nullptr));
  EXPECT_FALSE(validator().doVerifyCertChain(store_ctx.get(), nullptr, *cert, nullptr));
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
