#include <string>
#include <vector>

#include "envoy/admin/v3/certs.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.validate.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "source/common/common/base64.h"
#include "source/common/crypto/utility.h"
#include "source/common/json/json_loader.h"
#include "source/common/secret/sds_api.h"
#include "source/common/ssl/ssl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/tls/cert_validator/default_validator.h"
#include "source/common/tls/context_config_impl.h"
#include "source/common/tls/context_impl.h"
#include "source/common/tls/server_context_config_impl.h"
#include "source/common/tls/server_ssl_socket.h"
#include "source/common/tls/utility.h"

#include "test/common/tls/ssl_certs_test.h"
#include "test/common/tls/ssl_test_utility.h"
#include "test/common/tls/test_data/no_san_cert_info.h"
#include "test/common/tls/test_data/san_dns3_cert_info.h"
#include "test/common/tls/test_data/san_ip_cert_info.h"
#include "test/common/tls/test_data/unittest_cert_info.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/secret/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "openssl/crypto.h"
#include "openssl/x509v3.h"

using Envoy::Protobuf::util::MessageDifferencer;
using testing::EndsWith;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

namespace {
const std::vector<std::string>& knownCipherSuites() {
  CONSTRUCT_ON_FIRST_USE(std::vector<std::string>, {"ECDHE-ECDSA-AES128-GCM-SHA256",
                                                    "ECDHE-RSA-AES128-GCM-SHA256",
                                                    "ECDHE-ECDSA-AES256-GCM-SHA384",
                                                    "ECDHE-RSA-AES256-GCM-SHA384",
                                                    "ECDHE-ECDSA-CHACHA20-POLY1305",
                                                    "ECDHE-RSA-CHACHA20-POLY1305",
                                                    "ECDHE-PSK-CHACHA20-POLY1305",
                                                    "ECDHE-ECDSA-AES128-SHA",
                                                    "ECDHE-RSA-AES128-SHA",
                                                    "ECDHE-PSK-AES128-CBC-SHA",
                                                    "ECDHE-ECDSA-AES256-SHA",
                                                    "ECDHE-RSA-AES256-SHA",
                                                    "ECDHE-PSK-AES256-CBC-SHA",
                                                    "AES128-GCM-SHA256",
                                                    "AES256-GCM-SHA384",
                                                    "AES128-SHA",
                                                    "PSK-AES128-CBC-SHA",
                                                    "AES256-SHA",
                                                    "PSK-AES256-CBC-SHA",
                                                    "DES-CBC3-SHA"});
}

} // namespace

class SslLibraryCipherSuiteSupport : public ::testing::TestWithParam<std::string> {};

INSTANTIATE_TEST_SUITE_P(CipherSuites, SslLibraryCipherSuiteSupport,
                         ::testing::ValuesIn(knownCipherSuites()));

// Tests for whether new cipher suites are added. When they are, they must be added to
// knownCipherSuites() so that this test can detect if they are removed in the future.
// OpenSSL: Not sure how useful this test under OpenSSL is: cipher suites
// change from version to version, and also depend on the system-wide config.
// This is going to be a test-fail-fest. Disabling for now.
BORINGSSL_TEST_F(SslLibraryCipherSuiteSupport, CipherSuitesNotAdded) {
  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));
  EXPECT_NE(0, SSL_CTX_set_strict_cipher_list(ctx.get(), "ALL"));

  std::vector<std::string> present_cipher_suites;
  for (const SSL_CIPHER* cipher : SSL_CTX_get_ciphers(ctx.get())) {
    present_cipher_suites.push_back(SSL_CIPHER_get_name(cipher));
  }
  EXPECT_THAT(present_cipher_suites, testing::IsSubsetOf(knownCipherSuites()));
}

// Test that no previously supported cipher suites were removed from the SSL library. If a cipher
// suite is removed, it must be added to the release notes as an incompatible change, because it can
// cause previously loadable configurations to no longer load if they reference the cipher suite.
// OpenSSL: Disabled for the same reason as the CipherSuitesNotAdded test above.
BORINGSSL_TEST_P(SslLibraryCipherSuiteSupport, CipherSuitesNotRemoved) {
  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));
  EXPECT_NE(0, SSL_CTX_set_strict_cipher_list(ctx.get(), GetParam().c_str()));
}

class SslContextImplTest : public SslCertsTest {
public:
  ABSL_MUST_USE_RESULT Cleanup cleanUpHelper(Envoy::Ssl::ClientContextSharedPtr& context) {
    return {[&manager = manager_, &context]() {
      if (context != nullptr) {
        manager.removeContext(context);
      }
    }};
  }
  ABSL_MUST_USE_RESULT Cleanup cleanUpHelper(Envoy::Ssl::ServerContextSharedPtr& context) {
    return {[&manager = manager_, &context]() {
      if (context != nullptr) {
        manager.removeContext(context);
      }
    }};
  }
  void loadConfig(ServerContextConfigImpl& cfg) {
    Envoy::Ssl::ServerContextSharedPtr server_ctx(
        THROW_OR_RETURN_VALUE(manager_.createSslServerContext(*store_.rootScope(), cfg, nullptr),
                              Ssl::ServerContextSharedPtr));
    auto cleanup = cleanUpHelper(server_ctx);
  }

protected:
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  ContextManagerImpl manager_{server_factory_context_};
};

TEST_F(SslContextImplTest, TestCipherSuites) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites: "-ALL:+[AES128-SHA|BOGUS1-SHA256]:BOGUS2-SHA:AES256-SHA"
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);
  EXPECT_EQ(manager_.createSslClientContext(*store_.rootScope(), *cfg).status().message(),
            "Failed to initialize cipher suites "
            "-ALL:+[AES128-SHA|BOGUS1-SHA256]:BOGUS2-SHA:AES256-SHA. The following "
            "ciphers were rejected when tried individually: BOGUS1-SHA256, BOGUS2-SHA");
}

// Validates that multiple cipher-suites with the same contents are still equal
// after dedup.
TEST_F(SslContextImplTest, TestCipherSuitesDeduplication) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_params:
      cipher_suites: "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256"
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context1;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context1);
  auto cfg1 = *ClientContextConfigImpl::create(tls_context1, factory_context_);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context2;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context2);
  auto cfg2 = *ClientContextConfigImpl::create(tls_context2, factory_context_);

  EXPECT_EQ(cfg1->cipherSuites(), "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256");
  EXPECT_EQ(cfg2->cipherSuites(), "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256");

  EXPECT_OK(manager_.createSslClientContext(*store_.rootScope(), *cfg1));
  EXPECT_OK(manager_.createSslClientContext(*store_.rootScope(), *cfg2));
}

// Validates that TLS contexts referencing identical CRL content share a single
// parsed CRL, rather than each holding its own multi-megabyte parsed copy.
TEST_F(SslContextImplTest, TestCrlSharedAcrossContexts) {
  const std::string yaml = R"EOF(
  common_tls_context:
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      crl:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.crl"
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context1;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context1);
  auto cfg1 = *ClientContextConfigImpl::create(tls_context1, factory_context_);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context2;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context2);
  auto cfg2 = *ClientContextConfigImpl::create(tls_context2, factory_context_);

  auto ctx1 = *manager_.createSslClientContext(*store_.rootScope(), *cfg1);
  auto cleanup1 = cleanUpHelper(ctx1);
  auto ctx2 = *manager_.createSslClientContext(*store_.rootScope(), *cfg2);
  auto cleanup2 = cleanUpHelper(ctx2);
  ASSERT_NE(ctx1, nullptr);
  ASSERT_NE(ctx2, nullptr);

  // Both contexts reference the same CRL content, so it is parsed and held once.
  auto crl_cache = getCrlCache(server_factory_context_.singletonManager());
  EXPECT_EQ(crl_cache->size(), 1);

  // A context with a different CRL adds a second cache entry.
  const std::string other_yaml = R"EOF(
  common_tls_context:
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/intermediate_ca_cert.pem"
      crl:
        filename: "{{ test_rundir }}/test/common/tls/test_data/intermediate_ca_cert.crl"
  )EOF";
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context3;
  TestUtility::loadFromYaml(TestEnvironment::substitute(other_yaml), tls_context3);
  auto cfg3 = *ClientContextConfigImpl::create(tls_context3, factory_context_);
  auto ctx3 = *manager_.createSslClientContext(*store_.rootScope(), *cfg3);
  auto cleanup3 = cleanUpHelper(ctx3);
  ASSERT_NE(ctx3, nullptr);
  EXPECT_EQ(crl_cache->size(), 2);
}

// Validates the lifetime of a CRL shared between two TLS contexts: deleting one
// context leaves the CRL valid and still enforced for the other, and tearing
// down both contexts is safe. Primarily intended to run under ASAN/TSAN to catch
// lifetime regressions in the shared-CRL handling.
TEST_F(SslContextImplTest, TestCrlSharedContextLifetime) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      crl:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.crl"
  require_client_certificate: true
  )EOF";

  auto make_factory = [&]() {
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
    auto cfg = *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
    return *ServerSslSocketFactory::create(std::move(cfg), manager_, *store_.rootScope());
  };

  auto factory_a = make_factory();
  auto factory_b = make_factory();

  // Both contexts reference the same CRL content, so it is parsed and held once.
  auto crl_cache = getCrlCache(server_factory_context_.singletonManager());
  EXPECT_EQ(crl_cache->size(), 1);

  // A client certificate that ca_cert.crl revokes.
  bssl::UniquePtr<X509> revoked_cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));

  // Confirms the given context still enforces revocation through the shared CRL.
  auto expect_revocation_enforced = [&](ServerSslSocketFactory& factory) {
    Network::TransportSocketPtr socket = factory.createDownstreamTransportSocket();
    SSL_CTX* ssl_ctx = extractSslCtx(socket.get());
    bssl::UniquePtr<X509_STORE_CTX> store_ctx(X509_STORE_CTX_new());
    ASSERT_TRUE(X509_STORE_CTX_init(store_ctx.get(), SSL_CTX_get_cert_store(ssl_ctx),
                                    revoked_cert.get(), nullptr));
    EXPECT_EQ(X509_verify_cert(store_ctx.get()), 0);
    EXPECT_EQ(X509_STORE_CTX_get_error(store_ctx.get()), X509_V_ERR_CERT_REVOKED);
  };

  expect_revocation_enforced(*factory_b);

  // Delete one context; destroying the factory removes its context. The shared
  // CRL must remain valid and enforced for the surviving context.
  factory_a.reset();
  EXPECT_EQ(crl_cache->size(), 1);
  expect_revocation_enforced(*factory_b);

  // Delete the second context; the cache entry is released with no crash.
  factory_b.reset();
  EXPECT_EQ(crl_cache->size(), 0);
}

// Envoy's default cipher preference is server's.
TEST_F(SslContextImplTest, TestServerCipherPreference) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto cfg = ServerContextConfigImpl::create(tls_context, factory_context_, {}, false).value();
  ASSERT_FALSE(cfg.get()->preferClientCiphers());

  auto socket_factory = *Extensions::TransportSockets::Tls::ServerSslSocketFactory::create(
      std::move(cfg), manager_, *store_.rootScope());
  std::unique_ptr<Network::TransportSocket> socket =
      socket_factory->createDownstreamTransportSocket();
  SSL_CTX* ssl_ctx = extractSslCtx(socket.get());

  EXPECT_TRUE(SSL_CTX_get_options(ssl_ctx) & SSL_OP_CIPHER_SERVER_PREFERENCE);
}

TEST_F(SslContextImplTest, TestPreferClientCiphers) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
  prefer_client_ciphers: true
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto cfg = ServerContextConfigImpl::create(tls_context, factory_context_, {}, false).value();
  ASSERT_TRUE(cfg.get()->preferClientCiphers());

  auto socket_factory = *Extensions::TransportSockets::Tls::ServerSslSocketFactory::create(
      std::move(cfg), manager_, *store_.rootScope());
  std::unique_ptr<Network::TransportSocket> socket =
      socket_factory->createDownstreamTransportSocket();
  SSL_CTX* ssl_ctx = extractSslCtx(socket.get());

  EXPECT_FALSE(SSL_CTX_get_options(ssl_ctx) & SSL_OP_CIPHER_SERVER_PREFERENCE);
}

TEST_F(SslContextImplTest, TestExpiringCert) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
 )EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);

  auto cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);
  Envoy::Ssl::ClientContextSharedPtr context(
      *manager_.createSslClientContext(*store_.rootScope(), *cfg));
  auto cleanup = cleanUpHelper(context);
  // Calculate the days until test cert expires
  auto cert_expiry = TestUtility::parseTime(TEST_UNITTEST_CERT_NOT_AFTER, "%b %d %H:%M:%S %Y GMT");
  int64_t days_until_expiry = absl::ToInt64Hours(cert_expiry - absl::Now()) / 24;
  EXPECT_EQ(context->daysUntilFirstCertExpires().value(), days_until_expiry);
}

TEST_F(SslContextImplTest, TestExpiredCert) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/expired_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/expired_key.pem"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);
  Envoy::Ssl::ClientContextSharedPtr context(
      *manager_.createSslClientContext(*store_.rootScope(), *cfg));
  auto cleanup = cleanUpHelper(context);
  EXPECT_EQ(std::nullopt, context->daysUntilFirstCertExpires());
}

// Validate that when the context is updated, the daysUntilFirstCertExpires returns the current
// context value.
TEST_F(SslContextImplTest, TestContextUpdate) {
  const std::string expired_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/expired_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/expired_key.pem"
)EOF";

  // Validate that daysUntilFirstCertExpires returns correctly when single context is available.
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(expired_yaml), tls_context);
  auto cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);
  Envoy::Ssl::ClientContextSharedPtr context(
      *manager_.createSslClientContext(*store_.rootScope(), *cfg));
  EXPECT_EQ(manager_.daysUntilFirstCertExpires(), std::nullopt);

  const std::string expiring_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
 )EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext expiring_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(expiring_yaml), expiring_context);

  auto expirng_cfg = *ClientContextConfigImpl::create(expiring_context, factory_context_);

  Envoy::Ssl::ClientContextSharedPtr new_context(
      *manager_.createSslClientContext(*store_.rootScope(), *expirng_cfg));
  manager_.removeContext(context);

  // Validate that when the context is updated, daysUntilFirstCertExpires reflects the current
  // context expiry.
  auto cert_expiry = TestUtility::parseTime(TEST_UNITTEST_CERT_NOT_AFTER, "%b %d %H:%M:%S %Y GMT");
  int64_t days_until_expiry = absl::ToInt64Hours(cert_expiry - absl::Now()) / 24;
  EXPECT_EQ(new_context->daysUntilFirstCertExpires().value(), days_until_expiry);
  EXPECT_EQ(manager_.daysUntilFirstCertExpires().value(), days_until_expiry);

  // Update the context again and validate daysUntilFirstCertExpires still reflects the current
  // expiry.
  Envoy::Ssl::ClientContextSharedPtr updated_context(
      *manager_.createSslClientContext(*store_.rootScope(), *cfg));
  manager_.removeContext(new_context);
  auto cleanup = cleanUpHelper(updated_context);

  EXPECT_EQ(updated_context->daysUntilFirstCertExpires(), std::nullopt);
  EXPECT_EQ(manager_.daysUntilFirstCertExpires(), std::nullopt);
}

TEST_F(SslContextImplTest, TestGetCertInformation) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);

  Envoy::Ssl::ClientContextSharedPtr context(
      *manager_.createSslClientContext(*store_.rootScope(), *cfg));
  auto cleanup = cleanUpHelper(context);

  // This is similar to the hack above, but right now we generate the ca_cert and it expires in 15
  // days only in the first second that it's valid. We will partially match for up until Days until
  // Expiration: 1.
  // For the cert_chain, it is dynamically created when we run_envoy_test.sh which changes the
  // serial number with
  // every build. For cert_chain output, we check only for the certificate path.
  std::string ca_cert_json = absl::StrCat(R"EOF({
 "path": "{{ test_rundir }}/test/common/tls/test_data/no_san_cert.pem",
 "serial_number": ")EOF",
                                          TEST_NO_SAN_CERT_SERIAL, R"EOF(",
 "subject_alt_names": [],
 }
)EOF");

  std::string cert_chain_json = R"EOF({
 "path": "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem",
 }
)EOF";

  std::string ca_cert_partial_output(TestEnvironment::substitute(ca_cert_json));
  std::string cert_chain_partial_output(TestEnvironment::substitute(cert_chain_json));
  envoy::admin::v3::CertificateDetails certificate_details, cert_chain_details;
  TestUtility::loadFromJson(ca_cert_partial_output, certificate_details);
  TestUtility::loadFromJson(cert_chain_partial_output, cert_chain_details);

  MessageDifferencer message_differencer;
  message_differencer.set_scope(MessageDifferencer::Scope::PARTIAL);
  EXPECT_TRUE(message_differencer.Compare(certificate_details, *context->getCaCertInformation()));
  EXPECT_TRUE(
      message_differencer.Compare(cert_chain_details, *context->getCertChainInformation()[0]));
}

TEST_F(SslContextImplTest, TestGetCertInformationWithSAN) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_chain.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_cert.pem"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);

  Envoy::Ssl::ClientContextSharedPtr context(
      *manager_.createSslClientContext(*store_.rootScope(), *cfg));
  auto cleanup = cleanUpHelper(context);
  std::string ca_cert_json = absl::StrCat(R"EOF({
 "path": "{{ test_rundir }}/test/common/tls/test_data/san_dns3_cert.pem",
 "serial_number": ")EOF",
                                          TEST_SAN_DNS3_CERT_SERIAL, R"EOF(",
 "subject_alt_names": [
  {
   "dns": "server1.example.com"
  }
 ]
 }
)EOF");

  std::string cert_chain_json = R"EOF({
 "path": "{{ test_rundir }}/test/common/tls/test_data/san_dns3_chain.pem",
 }
)EOF";

  // This is similar to the hack above, but right now we generate the ca_cert and it expires in 15
  // days only in the first second that it's valid. We will partially match for up until Days until
  // Expiration: 1.
  // For the cert_chain, it is dynamically created when we run_envoy_test.sh which changes the
  // serial number with
  // every build. For cert_chain output, we check only for the certificate path.
  std::string ca_cert_partial_output(TestEnvironment::substitute(ca_cert_json));
  std::string cert_chain_partial_output(TestEnvironment::substitute(cert_chain_json));
  envoy::admin::v3::CertificateDetails certificate_details, cert_chain_details;
  TestUtility::loadFromJson(ca_cert_partial_output, certificate_details);
  TestUtility::loadFromJson(cert_chain_partial_output, cert_chain_details);

  MessageDifferencer message_differencer;
  message_differencer.set_scope(MessageDifferencer::Scope::PARTIAL);
  EXPECT_TRUE(message_differencer.Compare(certificate_details, *context->getCaCertInformation()));
  EXPECT_TRUE(
      message_differencer.Compare(cert_chain_details, *context->getCertChainInformation()[0]));
}

TEST_F(SslContextImplTest, TestGetCertInformationWithIPSAN) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_ip_chain.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_ip_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_ip_cert.pem"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);

  Envoy::Ssl::ClientContextSharedPtr context(
      *manager_.createSslClientContext(*store_.rootScope(), *cfg));
  auto cleanup = cleanUpHelper(context);
  std::string ca_cert_json = absl::StrCat(R"EOF({
 "path": "{{ test_rundir }}/test/common/tls/test_data/san_ip_cert.pem",
 "serial_number": ")EOF",
                                          TEST_SAN_IP_CERT_SERIAL, R"EOF(",
 "subject_alt_names": [
  {
   "ip_address": "1.1.1.1"
  }
 ]
 }
)EOF");

  std::string cert_chain_json = R"EOF({
 "path": "{{ test_rundir }}/test/common/tls/test_data/san_ip_chain.pem",
 }
)EOF";

  // This is similar to the hack above, but right now we generate the ca_cert and it expires in 15
  // days only in the first second that it's valid. We will partially match for up until Days until
  // Expiration: 1.
  // For the cert_chain, it is dynamically created when we run_envoy_test.sh which changes the
  // serial number with
  // every build. For cert_chain output, we check only for the certificate path.
  std::string ca_cert_partial_output(TestEnvironment::substitute(ca_cert_json));
  std::string cert_chain_partial_output(TestEnvironment::substitute(cert_chain_json));
  envoy::admin::v3::CertificateDetails certificate_details, cert_chain_details;
  TestUtility::loadFromJson(ca_cert_partial_output, certificate_details);
  TestUtility::loadFromJson(cert_chain_partial_output, cert_chain_details);

  MessageDifferencer message_differencer;
  message_differencer.set_scope(MessageDifferencer::Scope::PARTIAL);
  EXPECT_TRUE(message_differencer.Compare(certificate_details, *context->getCaCertInformation()));
  EXPECT_TRUE(
      message_differencer.Compare(cert_chain_details, *context->getCertChainInformation()[0]));
}

std::string convertTimeCertInfoToCertDetails(std::string cert_info_time) {
  return TestUtility::convertTime(cert_info_time, "%b %e %H:%M:%S %Y GMT", "%Y-%m-%dT%H:%M:%SZ");
}

TEST_F(SslContextImplTest, TestGetCertInformationWithExpiration) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_chain.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_cert.pem"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);

  Envoy::Ssl::ClientContextSharedPtr context(
      *manager_.createSslClientContext(*store_.rootScope(), *cfg));
  auto cleanup = cleanUpHelper(context);

  std::string ca_cert_json =
      absl::StrCat(R"EOF({
 "path": "{{ test_rundir }}/test/common/tls/test_data/san_dns3_cert.pem",
 "serial_number": ")EOF",
                   TEST_SAN_DNS3_CERT_SERIAL, R"EOF(",
 "subject_alt_names": [
  {
   "dns": "server1.example.com"
  }
 ],
 "valid_from": ")EOF",
                   convertTimeCertInfoToCertDetails(TEST_SAN_DNS3_CERT_NOT_BEFORE), R"EOF(",
 "expiration_time": ")EOF",
                   convertTimeCertInfoToCertDetails(TEST_SAN_DNS3_CERT_NOT_AFTER), R"EOF("
 }
)EOF");

  const std::string ca_cert_partial_output(TestEnvironment::substitute(ca_cert_json));
  envoy::admin::v3::CertificateDetails certificate_details;
  TestUtility::loadFromJson(ca_cert_partial_output, certificate_details);

  MessageDifferencer message_differencer;
  message_differencer.set_scope(MessageDifferencer::Scope::PARTIAL);
  EXPECT_TRUE(message_differencer.Compare(certificate_details, *context->getCaCertInformation()));
}

TEST_F(SslContextImplTest, TestNoCert) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext config;
  auto cfg = *ClientContextConfigImpl::create(config, factory_context_);
  Envoy::Ssl::ClientContextSharedPtr context(
      *manager_.createSslClientContext(*store_.rootScope(), *cfg));
  auto cleanup = cleanUpHelper(context);
  EXPECT_EQ(nullptr, context->getCaCertInformation());
  EXPECT_TRUE(context->getCertChainInformation().empty());
}

// Multiple RSA certificates with the same exact DNS SAN are allowed.
TEST_F(SslContextImplTest, DuplicateRsaCertSameExactDNSSan) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned2_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
  EXPECT_NO_THROW(loadConfig(*server_context_config));
}

// Multiple RSA certificates with the same wildcard DNS SAN are allowed.
TEST_F(SslContextImplTest, DuplicateRsaCertSameWildcardDNSSan) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_1_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_1_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
  EXPECT_NO_THROW(loadConfig(*server_context_config));
}

// Multiple RSA certificates with different exact DNS SAN are acceptable.
TEST_F(SslContextImplTest, AcceptableMultipleRsaCerts) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_1_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_1_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_2_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_rsa_2_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
  EXPECT_NO_THROW(loadConfig(*server_context_config));
}

// Multiple ECDSA certificates with the same exact DNS SAN are allowed.
TEST_F(SslContextImplTest, DuplicateEcdsaCert) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p256_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p256_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned2_ecdsa_p256_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p256_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
  EXPECT_NO_THROW(loadConfig(*server_context_config));
}

// Multiple ECDSA certificates with different DNS SAN are acceptable.
TEST_F(SslContextImplTest, AcceptableMultipleEcdsaCerts) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_ecdsa_1_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_ecdsa_1_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_ecdsa_2_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_ecdsa_2_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
  EXPECT_NO_THROW(loadConfig(*server_context_config));
}

// One cert which contains one of the SAN values in the CN is acceptable, because CN is not used if
// SANs are present.
TEST_F(SslContextImplTest, CertDuplicatedSansAndCN) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  // san_multiple_dns_1_cert's CN: server1.example.com
  // san_multiple_dns_1_cert's SAN: DNS.1 = *.example.com DNS.2 = server1.example.com
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_1_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_multiple_dns_1_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
  EXPECT_NO_THROW(loadConfig(*server_context_config));
}

// Multiple certificates with duplicated CN is acceptable, because CN is not used if SANs are
// present.
TEST_F(SslContextImplTest, MultipleCertsSansAndCN) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  // no_san_cn_cert's CN: server1.example.com
  // san_wildcard_dns_cert's CN: server1.example.com
  // san_wildcard_dns_cert's SAN: DNS.1 = *.example.com
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cn_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_san_cn_key.pem"
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_wildcard_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_wildcard_dns_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
  EXPECT_NO_THROW(loadConfig(*server_context_config));
}

// Certificates with no subject CN and no SANs are rejected.
TEST_F(SslContextImplTest, MustHaveSubjectOrSAN) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_subject_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/no_subject_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
  EXPECT_EQ(manager_.createSslServerContext(*store_.rootScope(), *server_context_config, nullptr)
                .status()
                .message(),
            "Invalid TLS context has neither subject CN nor SAN names");
}

class SslServerContextImplOcspTest : public SslContextImplTest {
public:
  Envoy::Ssl::ServerContextSharedPtr loadConfig(ServerContextConfigImpl& cfg) {
    return THROW_OR_RETURN_VALUE(manager_.createSslServerContext(*store_.rootScope(), cfg, nullptr),
                                 Ssl::ServerContextSharedPtr);
  }

  Envoy::Ssl::ServerContextSharedPtr loadConfigYaml(const std::string& yaml) {
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
    auto cfg = THROW_OR_RETURN_VALUE(
        ServerContextConfigImpl::create(tls_context, factory_context_, {}, false),
        std::unique_ptr<ServerContextConfigImpl>);
    return loadConfig(*cfg);
  }
};

TEST_F(SslServerContextImplOcspTest, TestFilenameOcspStapleConfigLoads) {
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_key.pem"
      ocsp_staple:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_ocsp_resp.der"
  ocsp_staple_policy: must_staple
  )EOF";
  auto context = loadConfigYaml(tls_context_yaml);
  auto cleanup = cleanUpHelper(context);
}

TEST_F(SslServerContextImplOcspTest, TestInlineBytesOcspStapleConfigLoads) {
  auto der_response = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_ocsp_resp.der"));
  auto base64_response = Base64::encode(der_response.c_str(), der_response.length(), true);
  const std::string tls_context_yaml = fmt::format(R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{{{ test_rundir }}}}/test/common/tls/ocsp/test_data/good_cert.pem"
      private_key:
        filename: "{{{{ test_rundir }}}}/test/common/tls/ocsp/test_data/good_key.pem"
      ocsp_staple:
       inline_bytes: "{}"
  ocsp_staple_policy: must_staple
  )EOF",
                                                   base64_response);

  auto context = loadConfigYaml(tls_context_yaml);
  auto cleanup = cleanUpHelper(context);
}

TEST_F(SslServerContextImplOcspTest, TestInlineStringOcspStapleConfigFails) {
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_key.pem"
      ocsp_staple:
       inline_string: "abcd"
  ocsp_staple_policy: must_staple
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(loadConfigYaml(tls_context_yaml), EnvoyException,
                            "OCSP staple cannot be provided via inline_string");
}

TEST_F(SslServerContextImplOcspTest, TestMismatchedOcspStapleConfigFails) {
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/revoked_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/revoked_key.pem"
      ocsp_staple:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_ocsp_resp.der"
  ocsp_staple_policy: must_staple
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(loadConfigYaml(tls_context_yaml), EnvoyException,
                            "OCSP response does not match its TLS certificate");
}

TEST_F(SslServerContextImplOcspTest, TestStaplingRequiredWithoutStapleConfigFails) {
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_key.pem"
  ocsp_staple_policy: must_staple
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(loadConfigYaml(tls_context_yaml), EnvoyException,
                            "Required OCSP response is missing from TLS context");
}

TEST_F(SslServerContextImplOcspTest, TestUnsuccessfulOcspResponseConfigFails) {
  std::vector<uint8_t> data = {
      // SEQUENCE
      0x30,
      3,
      // OcspResponseStatus - InternalError
      0xau,
      1,
      2,
      // no response bytes
  };
  std::string der_response(data.begin(), data.end());
  auto base64_response = Base64::encode(der_response.c_str(), der_response.length(), true);
  const std::string tls_context_yaml = fmt::format(R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{{{ test_rundir }}}}/test/common/tls/ocsp/test_data/good_cert.pem"
      private_key:
        filename: "{{{{ test_rundir }}}}/test/common/tls/ocsp/test_data/good_key.pem"
      ocsp_staple:
       inline_bytes: "{}"
  ocsp_staple_policy: must_staple
  )EOF",
                                                   base64_response);

  EXPECT_THROW_WITH_MESSAGE(loadConfigYaml(tls_context_yaml), EnvoyException,
                            "OCSP response was unsuccessful");
}

TEST_F(SslServerContextImplOcspTest, TestMustStapleCertWithoutStapleConfigFails) {
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/revoked_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/revoked_key.pem"
  ocsp_staple_policy: lenient_stapling
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(loadConfigYaml(tls_context_yaml), EnvoyException,
                            "OCSP response is required for must-staple certificate");
}

TEST_F(SslServerContextImplOcspTest, TestGetCertInformationWithOCSP) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_key.pem"
      ocsp_staple:
        filename: "{{ test_rundir }}/test/common/tls/ocsp/test_data/good_ocsp_resp.der"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto context = loadConfigYaml(yaml);
  auto cleanup = cleanUpHelper(context);

  constexpr absl::string_view this_update = "This Update: ";
  constexpr absl::string_view next_update = "Next Update: ";

  auto ocsp_text_details =
      absl::StrSplit(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
                         "{{ test_rundir "
                         "}}/test/common/tls/ocsp/test_data/good_ocsp_resp_details.txt")),
                     '\n');
  std::string valid_from, expiration;
  for (const auto& detail : ocsp_text_details) {
    std::string::size_type pos = detail.find(this_update);
    if (pos != std::string::npos) {
      valid_from = std::string(detail.substr(pos + this_update.size()));
      continue;
    }

    pos = detail.find(next_update);
    if (pos != std::string::npos) {
      expiration = std::string(detail.substr(pos + next_update.size()));
      continue;
    }
  }

  std::string ocsp_json = absl::StrCat(R"EOF({
"valid_from": ")EOF",
                                       convertTimeCertInfoToCertDetails(valid_from), R"EOF(",
"expiration": ")EOF",
                                       convertTimeCertInfoToCertDetails(expiration), R"EOF("
}
)EOF");

  envoy::admin::v3::CertificateDetails::OcspDetails ocsp_details;
  TestUtility::loadFromJson(ocsp_json, ocsp_details);

  MessageDifferencer message_differencer;
  message_differencer.set_scope(MessageDifferencer::Scope::PARTIAL);
  EXPECT_TRUE(message_differencer.Compare(ocsp_details,
                                          context->getCertChainInformation()[0]->ocsp_details()));
}

class SslServerContextImplTicketTest : public SslContextImplTest {
public:
  void loadConfig(ServerContextConfigImpl& cfg) {
    Envoy::Ssl::ServerContextSharedPtr server_ctx(
        THROW_OR_RETURN_VALUE(manager_.createSslServerContext(*store_.rootScope(), cfg, nullptr),
                              Ssl::ServerContextSharedPtr));
    auto cleanup = cleanUpHelper(server_ctx);
  }

  void loadConfigV2(envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext& cfg) {
    // Must add a certificate for the config to be considered valid.
    envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
        cfg.mutable_common_tls_context()->add_tls_certificates();
    server_cert->mutable_certificate_chain()->set_filename(TestEnvironment::substitute(
        "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"));
    server_cert->mutable_private_key()->set_filename(TestEnvironment::substitute(
        "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"));

    auto server_context_config =
        THROW_OR_RETURN_VALUE(ServerContextConfigImpl::create(cfg, factory_context_, {}, false),
                              std::unique_ptr<ServerContextConfigImpl>);
    loadConfig(*server_context_config);
  }

  void loadConfigYaml(const std::string& yaml) {
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
    auto cfg = THROW_OR_RETURN_VALUE(
        ServerContextConfigImpl::create(tls_context, factory_context_, {}, false),
        std::unique_ptr<ServerContextConfigImpl>);
    loadConfig(*cfg);
  }
};

// If no require_client_certificate is configured but a validation context IS configured, warn
// against using an insecure default.
TEST_F(SslContextImplTest, NoRequireClientCertWithValidationContext_InsecureDefault) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  EXPECT_CALL(factory_context_.server_context_.runtime_loader_.snapshot_,
              deprecatedFeatureEnabled(_, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(factory_context_.server_context_.runtime_loader_, countDeprecatedFeatureUse());
  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
  EXPECT_NO_THROW(loadConfig(*server_context_config));
}

TEST_F(SslServerContextImplTicketTest, TicketKeySuccess) {
  // Both keys are valid; no error should be thrown
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_b"
)EOF";
  loadConfigYaml(yaml);
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInvalidLen) {
  // First key is valid, second key isn't. Should throw if any keys are invalid.
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_wrong_len"
)EOF";
  EXPECT_THROW(loadConfigYaml(yaml), EnvoyException);
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInvalidCannotRead) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/this_file_does_not_exist"
)EOF";
  EXPECT_THROW(loadConfigYaml(yaml), std::exception);
}

TEST_F(SslServerContextImplTicketTest, TicketKeyNone) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext cfg;
  EXPECT_NO_THROW(loadConfigV2(cfg));
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInlineBytesSuccess) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext cfg;
  cfg.mutable_session_ticket_keys()->add_keys()->set_inline_bytes(std::string(80, '\0'));
  EXPECT_NO_THROW(loadConfigV2(cfg));
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInlineStringSuccess) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext cfg;
  cfg.mutable_session_ticket_keys()->add_keys()->set_inline_string(std::string(80, '\0'));
  EXPECT_NO_THROW(loadConfigV2(cfg));
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInlineBytesFailTooBig) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext cfg;
  cfg.mutable_session_ticket_keys()->add_keys()->set_inline_bytes(std::string(81, '\0'));
  EXPECT_THROW(loadConfigV2(cfg), EnvoyException);
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInlineStringFailTooBig) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext cfg;
  cfg.mutable_session_ticket_keys()->add_keys()->set_inline_string(std::string(81, '\0'));
  EXPECT_THROW(loadConfigV2(cfg), EnvoyException);
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInlineBytesFailTooSmall) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext cfg;
  cfg.mutable_session_ticket_keys()->add_keys()->set_inline_bytes(std::string(79, '\0'));
  EXPECT_THROW(loadConfigV2(cfg), EnvoyException);
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInlineStringFailTooSmall) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext cfg;
  cfg.mutable_session_ticket_keys()->add_keys()->set_inline_string(std::string(79, '\0'));
  EXPECT_THROW(loadConfigV2(cfg), EnvoyException);
}

TEST_F(SslServerContextImplTicketTest, TicketKeySdsNotReady) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"));
  server_cert->mutable_private_key()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"));

  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Random::MockRandomGenerator> random;
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  NiceMock<Init::MockManager> init_manager;
  EXPECT_CALL(factory_context_.server_context_, localInfo()).WillOnce(ReturnRef(local_info));
  EXPECT_CALL(factory_context_.server_context_, mainThreadDispatcher())
      .WillRepeatedly(ReturnRef(dispatcher));
  // EXPECT_CALL(factory_context_, random()).WillOnce(ReturnRef(random));
  EXPECT_CALL(factory_context_.server_context_, clusterManager())
      .WillOnce(ReturnRef(cluster_manager));
  EXPECT_CALL(factory_context_, initManager()).WillRepeatedly(ReturnRef(init_manager));
  auto* sds_secret_configs = tls_context.mutable_session_ticket_keys_sds_secret_config();
  sds_secret_configs->set_name("abc.com");
  sds_secret_configs->mutable_sds_config();
  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
  // When sds secret is not downloaded, config is not ready.
  EXPECT_FALSE(server_context_config->isReady());
  // Set various callbacks to config.
  NiceMock<Secret::MockSecretCallbacks> secret_callback;
  server_context_config->setSecretUpdateCallback(
      [&secret_callback]() { return secret_callback.onAddOrUpdateSecret(); });
  server_context_config->setSecretUpdateCallback([]() { return absl::OkStatus(); });
}

// Validate that client context config with static TLS ticket encryption keys is created
// successfully.
TEST_F(SslServerContextImplTicketTest, StaticTickeyKey) {
  envoy::extensions::transport_sockets::tls::v3::Secret secret_config;

  const std::string yaml = R"EOF(
name: "abc.com"
session_ticket_keys:
  keys:
    - filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
    - filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_b"
)EOF";

  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);
  EXPECT_OK(factory_context_.server_context_.secretManager().addStaticSecret(secret_config));

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"));
  server_cert->mutable_private_key()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"));

  tls_context.mutable_session_ticket_keys_sds_secret_config()->set_name("abc.com");

  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);

  EXPECT_TRUE(server_context_config->isReady());
  ASSERT_EQ(server_context_config->sessionTicketKeys().size(), 2);
}

TEST_F(SslServerContextImplTicketTest, CRLSuccess) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      crl:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.crl"
)EOF";
  EXPECT_NO_THROW(loadConfigYaml(yaml));
}

TEST_F(SslServerContextImplTicketTest, CRLInvalid) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
      crl:
        filename: "{{ test_rundir }}/test/common/tls/test_data/not_a_crl.crl"
)EOF";
  EXPECT_THROW_WITH_REGEX(loadConfigYaml(yaml), EnvoyException,
                          "^Failed to load CRL from .*/not_a_crl.crl$");
}

TEST_F(SslServerContextImplTicketTest, CRLWithNoCA) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
    validation_context:
      crl:
        filename: "{{ test_rundir }}/test/common/tls/test_data/not_a_crl.crl"
)EOF";
  EXPECT_THROW_WITH_REGEX(loadConfigYaml(yaml), EnvoyException,
                          "^Failed to load CRL from .* without trusted CA$");
}

TEST_F(SslServerContextImplTicketTest, VerifySanWithNoCA) {
  const std::string yaml = R"EOF(
       common_tls_context:
          tls_certificates:
            certificate_chain:
              filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
            private_key:
              filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
          validation_context:
            match_typed_subject_alt_names:
            - san_type: URI
              matcher:
                exact: "spiffe://lyft.com/testclient"
)EOF";
  EXPECT_THROW_WITH_MESSAGE(loadConfigYaml(yaml), EnvoyException,
                            "SAN-based verification of peer certificates without trusted CA "
                            "is insecure and not allowed");
}

TEST_F(SslServerContextImplTicketTest, EmptyTrustedCA) {
  const std::string empty_ca_path = TestEnvironment::writeStringToFileForTest("test_envoy", "");
  const std::string yaml = fmt::format(R"EOF(
    common_tls_context:
      tls_certificates:
        certificate_chain:
          filename: "{{{{ test_rundir }}}}/test/common/tls/test_data/san_dns_cert.pem"
        private_key:
          filename: "{{{{ test_rundir }}}}/test/common/tls/test_data/san_dns_key.pem"
      validation_context:
        trusted_ca:
          filename: "{}"
  )EOF",
                                       empty_ca_path);
  EXPECT_THROW_WITH_MESSAGE(loadConfigYaml(yaml), EnvoyException,
                            fmt::format("file {} is empty", empty_ca_path));
}

TEST_F(SslServerContextImplTicketTest, EmptyTrustedCAInlineString) {
  const std::string yaml = R"EOF(
    common_tls_context:
      tls_certificates:
        certificate_chain:
          filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
        private_key:
          filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
      validation_context:
        trusted_ca:
          inline_string: ""
  )EOF";
  EXPECT_THROW_WITH_MESSAGE(loadConfigYaml(yaml), EnvoyException, "DataSource cannot be empty");
}

TEST_F(SslServerContextImplTicketTest, EmptyTrustedCAInlineBytes) {
  const std::string yaml = R"EOF(
    common_tls_context:
      tls_certificates:
        certificate_chain:
          filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
        private_key:
          filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
      validation_context:
        trusted_ca:
          inline_bytes: ""
  )EOF";
  EXPECT_THROW_WITH_MESSAGE(loadConfigYaml(yaml), EnvoyException, "DataSource cannot be empty");
}

TEST_F(SslServerContextImplTicketTest, EmptyTrustedCAWhenRuntimeDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.reject_empty_trusted_ca_file", "false"}});
  const std::string empty_ca_path = TestEnvironment::writeStringToFileForTest("test_envoy", "");
  const std::string yaml = fmt::format(R"EOF(
    common_tls_context:
      tls_certificates:
        certificate_chain:
          filename: "{{{{ test_rundir }}}}/test/common/tls/test_data/san_dns_cert.pem"
        private_key:
          filename: "{{{{ test_rundir }}}}/test/common/tls/test_data/san_dns_key.pem"
      validation_context:
        trusted_ca:
          filename: "{}"
  )EOF",
                                       empty_ca_path);
  EXPECT_NO_THROW(loadConfigYaml(yaml));
}

TEST_F(SslServerContextImplTicketTest, EmptyTrustedCAInlineStringWhenRuntimeDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.reject_empty_trusted_ca_file", "false"}});
  const std::string yaml = R"EOF(
    common_tls_context:
      tls_certificates:
        certificate_chain:
          filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
        private_key:
          filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
      validation_context:
        trusted_ca:
          inline_string: ""
  )EOF";
  EXPECT_NO_THROW(loadConfigYaml(yaml));
}

TEST_F(SslServerContextImplTicketTest, EmptyTrustedCAInlineByteWhenRuntimeDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.reject_empty_trusted_ca_file", "false"}});
  const std::string yaml = R"EOF(
    common_tls_context:
      tls_certificates:
        certificate_chain:
          filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
        private_key:
          filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
      validation_context:
        trusted_ca:
          inline_bytes: ""
  )EOF";
  EXPECT_NO_THROW(loadConfigYaml(yaml));
}

TEST_F(SslServerContextImplTicketTest, ValidationContextWithPinnedCertificateHash) {
  const std::string yaml = R"EOF(
    common_tls_context:
      tls_certificates:
        certificate_chain:
          filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"
        private_key:
          filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns_key.pem"
      validation_context:
        verify_certificate_hash: "df6ff72fe9116521268f6f2dd4966f51df479883fe7037b39f75916ac3049d1a"
  )EOF";
  EXPECT_NO_THROW(loadConfigYaml(yaml));
}

TEST_F(SslServerContextImplTicketTest, StatelessSessionResumptionEnabledByDefault) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);

  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
  EXPECT_FALSE(server_context_config->disableStatelessSessionResumption());
}

TEST_F(SslServerContextImplTicketTest, StatelessSessionResumptionExplicitlyEnabled) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
  disable_stateless_session_resumption: false
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);

  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
  EXPECT_FALSE(server_context_config->disableStatelessSessionResumption());
}

TEST_F(SslServerContextImplTicketTest, StatelessSessionResumptionDisabled) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
  disable_stateless_session_resumption: true
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);

  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
  EXPECT_TRUE(server_context_config->disableStatelessSessionResumption());
}

TEST_F(SslServerContextImplTicketTest, StatelessSessionResumptionEnabledWhenKeyIsConfigured) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
  session_ticket_keys:
    keys:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"
)EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);

  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
  EXPECT_FALSE(server_context_config->disableStatelessSessionResumption());
}

class ServerContextConfigImplTest : public SslCertsTest {
public:
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
};

// Multiple TLS certificates are supported.
TEST_F(ServerContextConfigImplTest, MultipleTlsCertificates) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  EXPECT_EQ(
      ServerContextConfigImpl::create(tls_context, factory_context_, {}, false).status().message(),
      "No TLS certificates found for server context");
  const std::string rsa_tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
  )EOF";
  const std::string ecdsa_tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p256_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p256_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(rsa_tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  TestUtility::loadFromYaml(TestEnvironment::substitute(ecdsa_tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
  auto tls_certs = server_context_config->tlsCertificates();
  ASSERT_EQ(2, tls_certs.size());
  EXPECT_THAT(tls_certs[0].get().privateKeyPath(), EndsWith("selfsigned_key.pem"));
  EXPECT_THAT(tls_certs[1].get().privateKeyPath(), EndsWith("selfsigned_ecdsa_p256_key.pem"));
}

TEST_F(ServerContextConfigImplTest, TlsCertificatesAndSdsConfig) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  EXPECT_EQ(
      ServerContextConfigImpl::create(tls_context, factory_context_, {}, false).status().message(),
      "No TLS certificates found for server context");
  const std::string tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  tls_context.mutable_common_tls_context()->add_tls_certificate_sds_secret_configs();
  EXPECT_EQ(
      ServerContextConfigImpl::create(tls_context, factory_context_, {}, false).status().message(),
      "SDS and non-SDS TLS certificates may not be mixed in server contexts");
}

TEST_F(ServerContextConfigImplTest, SdsConfigNoName) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()->add_tls_certificate_sds_secret_configs();
  EXPECT_THROW_WITH_REGEX(
      TestUtility::validate<envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext>(
          tls_context),
      EnvoyException, "Proto constraint validation failed");
}

TEST_F(ServerContextConfigImplTest, MultiSdsConfig) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()->add_tls_certificate_sds_secret_configs()->set_name(
      "server_cert1");
  tls_context.mutable_common_tls_context()->add_tls_certificate_sds_secret_configs()->set_name(
      "server_cert2");
  tls_context.mutable_common_tls_context()->add_tls_certificate_sds_secret_configs()->set_name(
      "server_cert3");
  EXPECT_NO_THROW(
      TestUtility::validate<envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext>(
          tls_context));
}

TEST_F(ServerContextConfigImplTest, SecretNotReady) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Event::MockDispatcher> dispatcher;
  EXPECT_CALL(factory_context_.server_context_, localInfo()).WillOnce(ReturnRef(local_info));
  EXPECT_CALL(factory_context_, initManager()).WillRepeatedly(ReturnRef(init_manager));
  EXPECT_CALL(factory_context_.server_context_, mainThreadDispatcher())
      .WillRepeatedly(ReturnRef(dispatcher));
  auto sds_secret_configs =
      tls_context.mutable_common_tls_context()->mutable_tls_certificate_sds_secret_configs()->Add();
  sds_secret_configs->set_name("abc.com");
  sds_secret_configs->mutable_sds_config();
  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
  // When sds secret is not downloaded, config is not ready.
  EXPECT_FALSE(server_context_config->isReady());
  // Set various callbacks to config.
  NiceMock<Secret::MockSecretCallbacks> secret_callback;
  server_context_config->setSecretUpdateCallback(
      [&secret_callback]() { return secret_callback.onAddOrUpdateSecret(); });
  server_context_config->setSecretUpdateCallback([]() { return absl::OkStatus(); });
}

// Validate server context config supports SDS, and is marked as not ready if dynamic
// certificate validation context is not yet downloaded.
TEST_F(ServerContextConfigImplTest, ValidationContextNotReady) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"));
  server_cert->mutable_private_key()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Event::MockDispatcher> dispatcher;
  EXPECT_CALL(factory_context_.server_context_, localInfo()).WillOnce(ReturnRef(local_info));
  EXPECT_CALL(factory_context_, initManager()).WillRepeatedly(ReturnRef(init_manager));
  EXPECT_CALL(factory_context_.server_context_, mainThreadDispatcher())
      .WillRepeatedly(ReturnRef(dispatcher));
  auto sds_secret_configs =
      tls_context.mutable_common_tls_context()->mutable_validation_context_sds_secret_config();
  sds_secret_configs->set_name("abc.com");
  sds_secret_configs->mutable_sds_config();
  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
  // When sds secret is not downloaded, config is not ready.
  EXPECT_FALSE(server_context_config->isReady());
  // Set various callbacks to config.
  NiceMock<Secret::MockSecretCallbacks> secret_callback;
  server_context_config->setSecretUpdateCallback(
      [&secret_callback]() { return secret_callback.onAddOrUpdateSecret(); });
  server_context_config->setSecretUpdateCallback([]() { return absl::OkStatus(); });
}

// TlsCertificate messages must have a cert for servers.
TEST_F(ServerContextConfigImplTest, TlsCertificateNonEmpty) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()->add_tls_certificates();
  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
  ContextManagerImpl manager(server_factory_context_);
  Stats::IsolatedStoreImpl store;
  EXPECT_EQ(manager.createSslServerContext(*store.rootScope(), *server_context_config, nullptr)
                .status()
                .message(),
            "Server TlsCertificates must have a certificate specified");
}

// Cannot ignore certificate expiration without a trusted CA.
TEST_F(ServerContextConfigImplTest, InvalidIgnoreCertsNoCA) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;

  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
      server_validation_ctx =
          tls_context.mutable_common_tls_context()->mutable_validation_context();

  server_validation_ctx->set_allow_expired_certificate(true);

  EXPECT_EQ(
      ServerContextConfigImpl::create(tls_context, factory_context_, {}, false).status().message(),
      "Certificate validity period is always ignored without trusted CA");

  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* server_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  server_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"));
  server_cert->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"));

  server_validation_ctx->set_allow_expired_certificate(false);

  EXPECT_NO_THROW(auto server_context_config =
                      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false));

  server_validation_ctx->set_allow_expired_certificate(true);

  EXPECT_EQ(
      ServerContextConfigImpl::create(tls_context, factory_context_, {}, false).status().message(),
      "Certificate validity period is always ignored without trusted CA");

  // But once you add a trusted CA, you should be able to create the context.
  server_validation_ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));

  EXPECT_NO_THROW(auto server_context_config =
                      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false));
}

TEST_F(ServerContextConfigImplTest, PrivateKeyMethodLoadFailureNoProvider) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  NiceMock<Ssl::MockContextManager> context_manager;
  NiceMock<Ssl::MockPrivateKeyMethodManager> private_key_method_manager;
  EXPECT_CALL(factory_context_.server_context_, sslContextManager())
      .WillOnce(ReturnRef(context_manager));
  EXPECT_CALL(context_manager, privateKeyMethodManager())
      .WillOnce(ReturnRef(private_key_method_manager));
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
      private_key_provider:
        provider_name: mock_provider
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            test_value: 100
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  EXPECT_EQ(
      ServerContextConfigImpl::create(tls_context, factory_context_, {}, false).status().message(),
      "Failed to load private key provider: mock_provider");
}

TEST_F(ServerContextConfigImplTest, PrivateKeyMethodLoadFailureNoProviderFallback) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  NiceMock<Ssl::MockContextManager> context_manager;
  NiceMock<Ssl::MockPrivateKeyMethodManager> private_key_method_manager;
  EXPECT_CALL(factory_context_.server_context_, sslContextManager())
      .WillOnce(ReturnRef(context_manager));
  EXPECT_CALL(context_manager, privateKeyMethodManager())
      .WillOnce(ReturnRef(private_key_method_manager));
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
      private_key_provider:
        provider_name: mock_provider
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            test_value: 100
        fallback: true
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  EXPECT_EQ(
      ServerContextConfigImpl::create(tls_context, factory_context_, {}, false).status().message(),
      "Failed to load private key provider: mock_provider");
}

TEST_F(ServerContextConfigImplTest, PrivateKeyMethodLoadFailureNoMethod) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()->add_tls_certificates();
  Stats::IsolatedStoreImpl store;
  NiceMock<Ssl::MockContextManager> context_manager;
  NiceMock<Ssl::MockPrivateKeyMethodManager> private_key_method_manager;
  auto private_key_method_provider_ptr =
      std::make_shared<NiceMock<Ssl::MockPrivateKeyMethodProvider>>();
  ContextManagerImpl manager(server_factory_context_);
  EXPECT_CALL(factory_context_.server_context_, sslContextManager())
      .WillOnce(ReturnRef(context_manager));
  EXPECT_CALL(context_manager, privateKeyMethodManager())
      .WillOnce(ReturnRef(private_key_method_manager));
  EXPECT_CALL(private_key_method_manager, createPrivateKeyMethodProvider(_, _))
      .WillOnce(Return(private_key_method_provider_ptr));
  EXPECT_CALL(*private_key_method_provider_ptr, isAvailable()).WillRepeatedly(Return(true));
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
      private_key_provider:
        provider_name: mock_provider
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            test_value: 100
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
  EXPECT_EQ(manager.createSslServerContext(*store.rootScope(), *server_context_config, nullptr)
                .status()
                .message(),
            "Failed to get BoringSSL private key method from provider");
}

TEST_F(ServerContextConfigImplTest, PrivateKeyMethodLoadSuccess) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  NiceMock<Ssl::MockContextManager> context_manager;
  NiceMock<Ssl::MockPrivateKeyMethodManager> private_key_method_manager;
  auto private_key_method_provider_ptr =
      std::make_shared<NiceMock<Ssl::MockPrivateKeyMethodProvider>>();
  EXPECT_CALL(factory_context_.server_context_, sslContextManager())
      .WillOnce(ReturnRef(context_manager));
  EXPECT_CALL(context_manager, privateKeyMethodManager())
      .WillOnce(ReturnRef(private_key_method_manager));
  EXPECT_CALL(private_key_method_manager, createPrivateKeyMethodProvider(_, _))
      .WillOnce(Return(private_key_method_provider_ptr));
  EXPECT_CALL(*private_key_method_provider_ptr, isAvailable()).WillRepeatedly(Return(true));
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
      private_key_provider:
        provider_name: mock_provider
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            test_value: 100
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
}

TEST_F(ServerContextConfigImplTest, PrivateKeyMethodFallback) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  NiceMock<Ssl::MockContextManager> context_manager;
  NiceMock<Ssl::MockPrivateKeyMethodManager> private_key_method_manager;
  auto private_key_method_provider_ptr =
      std::make_shared<NiceMock<Ssl::MockPrivateKeyMethodProvider>>();
  EXPECT_CALL(factory_context_.server_context_, sslContextManager())
      .WillOnce(ReturnRef(context_manager));
  EXPECT_CALL(context_manager, privateKeyMethodManager())
      .WillOnce(ReturnRef(private_key_method_manager));
  EXPECT_CALL(private_key_method_manager, createPrivateKeyMethodProvider(_, _))
      .WillOnce(Return(private_key_method_provider_ptr));
  EXPECT_CALL(*private_key_method_provider_ptr, isAvailable()).WillRepeatedly(Return(false));
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
      private_key_provider:
        provider_name: mock_provider
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            test_value: 100
        fallback: true
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);
}

// Test that if both typed and untyped matchers for sans are specified, we
// ignore the untyped matchers.
TEST_F(ServerContextConfigImplTest, DeprecatedSanMatcher) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  NiceMock<Ssl::MockContextManager> context_manager;
  NiceMock<Ssl::MockPrivateKeyMethodManager> private_key_method_manager;
  auto private_key_method_provider_ptr =
      std::make_shared<NiceMock<Ssl::MockPrivateKeyMethodProvider>>();
  const std::string yaml =
      R"EOF(
      common_tls_context:
        tls_certificates:
        - certificate_chain:
            filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
          private_key:
            filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
        validation_context:
          trusted_ca: { filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem" }
          allow_expired_certificate: true
          match_typed_subject_alt_names:
          - san_type: DNS
            matcher:
              exact: "foo1.example"
          match_subject_alt_names:
            exact: "foo2.example"
      )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);

  std::unique_ptr<ServerContextConfigImpl> server_context_config;
  EXPECT_LOG_CONTAINS("warning",
                      "Ignoring match_subject_alt_names as match_typed_subject_alt_names is also "
                      "specified, and the former is deprecated.",
                      server_context_config = *ServerContextConfigImpl::create(
                          tls_context, factory_context_, {}, false));
  EXPECT_EQ(server_context_config->certificateValidationContext()->subjectAltNameMatchers().size(),
            1);
  EXPECT_EQ(
      server_context_config->certificateValidationContext()->subjectAltNameMatchers()[0].san_type(),
      envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::DNS);
  EXPECT_EQ(server_context_config->certificateValidationContext()
                ->subjectAltNameMatchers()[0]
                .matcher()
                .exact(),
            "foo1.example");
}

TEST_F(ServerContextConfigImplTest, Pkcs12LoadFailureBothPkcs12AndMethod) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  NiceMock<Ssl::MockContextManager> context_manager;
  NiceMock<Ssl::MockPrivateKeyMethodManager> private_key_method_manager;
  auto private_key_method_provider_ptr =
      std::make_shared<NiceMock<Ssl::MockPrivateKeyMethodProvider>>();
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - pkcs12:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_certkeychain.p12"
      private_key_provider:
        provider_name: mock_provider
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            test_value: 100
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  EXPECT_EQ(
      ServerContextConfigImpl::create(tls_context, factory_context_, {}, false).status().message(),
      "Certificate configuration can't have both pkcs12 and private_key_provider");
}

TEST_F(ServerContextConfigImplTest, Pkcs12LoadFailureBothPkcs12AndKey) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - pkcs12:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_certkeychain.p12"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  EXPECT_EQ(
      ServerContextConfigImpl::create(tls_context, factory_context_, {}, false).status().message(),
      "Certificate configuration can't have both pkcs12 and private_key");
}

TEST_F(ServerContextConfigImplTest, Pkcs12LoadFailureBothPkcs12AndCertChain) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const std::string tls_context_yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
      pkcs12:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_dns3_certkeychain.p12"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_context_yaml), tls_context);
  EXPECT_EQ(
      ServerContextConfigImpl::create(tls_context, factory_context_, {}, false).status().message(),
      "Certificate configuration can't have both pkcs12 and certificate_chain");
}

// TODO: test throw from additional_init

// Subclass ContextImpl so we can instantiate directly from tests, despite the
// constructor being protected.
class TestContextImpl : public ContextImpl {
public:
  TestContextImpl(Stats::Scope& scope, const Envoy::Ssl::ContextConfig& config,
                  Server::Configuration::ServerFactoryContext& factory_context,
                  absl::Status& creation_status)
      : ContextImpl(scope, config, config.tlsCertificates(), factory_context, nullptr,
                    creation_status),
        pool_(scope.symbolTable()), fallback_(pool_.add("fallback")) {}

  void incCounter(absl::string_view name, absl::string_view value) {
    ContextImpl::incCounter(pool_.add(name), value, fallback_);
  }

  Stats::StatNamePool pool_;
  const Stats::StatName fallback_;
};

class SslContextStatsTest : public SslContextImplTest {
protected:
  SslContextStatsTest() {
    TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context_);
    client_context_config_ = *ClientContextConfigImpl::create(tls_context_, factory_context_);
    absl::Status creation_status = absl::OkStatus();
    context_ = std::make_unique<TestContextImpl>(*store_.rootScope(), *client_context_config_,
                                                 server_factory_context_, creation_status);
    EXPECT_OK(creation_status);
  }

  Stats::TestUtil::TestStore store_;
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context_;
  std::unique_ptr<ClientContextConfigImpl> client_context_config_;
  std::unique_ptr<TestContextImpl> context_;
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"
  )EOF";
};

TEST_F(SslContextStatsTest, IncOnlyKnownCounters) {
  // Incrementing a value for a cipher that is part of the configuration works, and
  // we'll be able to find the value in the stats store.
  for (const auto& cipher :
       {"TLS_AES_128_GCM_SHA256", "TLS_AES_256_GCM_SHA384", "TLS_CHACHA20_POLY1305_SHA256"}) {
    // Test supported built-in TLS v1.3 cipher suites
    // https://tools.ietf.org/html/rfc8446#appendix-B.4.
    context_->incCounter("ssl.ciphers", cipher);
    Stats::CounterOptConstRef stat =
        store_.findCounterByString(absl::StrCat("ssl.ciphers.", cipher));
    ASSERT_TRUE(stat.has_value());
    EXPECT_EQ(1, stat->get().value());
  }

  // Incrementing a stat for a random unknown cipher does not work. A
  // rate-limited error log message will also be generated but that is hard to
  // test as it is dependent on timing and test-ordering.
  EXPECT_DEBUG_DEATH(context_->incCounter("ssl.ciphers", "unexpected"),
                     "Unexpected ssl.ciphers value: unexpected");
  EXPECT_FALSE(store_.findCounterByString("ssl.ciphers.unexpected"));

  // We will account for the 'unexpected' cipher as "fallback", however in debug
  // mode that will not work as the ENVOY_BUG macro will assert first, thus the
  // fallback registration does not occur. So we test for the fallback only in
  // release builds.
#ifdef NDEBUG
  Stats::CounterOptConstRef stat = store_.findCounterByString("ssl.ciphers.fallback");
  ASSERT_TRUE(stat.has_value());
  EXPECT_EQ(1, stat->get().value());
#endif
}

class CertificateNamingTest : public SslCertsTest {};

TEST_F(CertificateNamingTest, TlsCertificateInlineNaming) {
  std::string cert_data = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"));

  // Setup CommonTlsContext with inline certificate
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  auto* tls_cert = tls_context.mutable_common_tls_context()->add_tls_certificates();
  tls_cert->mutable_certificate_chain()->set_inline_bytes(cert_data);
  tls_cert->mutable_private_key()->set_inline_string("dummy_key");

  // Calculate expected hash
  Buffer::OwnedImpl buffer(cert_data);
  std::string expected_hash =
      Hex::encode(Envoy::Common::Crypto::UtilitySingleton::get().getSha256Digest(buffer))
          .substr(0, 8);

  // Create and check the context config
  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);

  auto tls_certs = server_context_config->tlsCertificates();
  ASSERT_EQ(1, tls_certs.size());
  EXPECT_EQ("unnamed_cert_" + expected_hash, tls_certs[0].get().certificateName());
}

TEST_F(CertificateNamingTest, CACertificateInlineNaming) {
  std::string ca_cert_data = TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));
  std::string tls_cert_data = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"));
  std::string key_data = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"));

  // Setup context with both TLS cert and CA cert
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;

  // Add TLS certificate first (required for server context)
  auto* tls_cert = tls_context.mutable_common_tls_context()->add_tls_certificates();
  tls_cert->mutable_certificate_chain()->set_inline_bytes(tls_cert_data);
  tls_cert->mutable_private_key()->set_inline_bytes(key_data);

  tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->mutable_trusted_ca()
      ->set_inline_bytes(ca_cert_data);

  // Calculate expected hash
  Buffer::OwnedImpl buffer(ca_cert_data);
  std::string expected_hash =
      Hex::encode(Envoy::Common::Crypto::UtilitySingleton::get().getSha256Digest(buffer))
          .substr(0, 8);

  // Create the context config
  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);

  // Verify the CA cert name
  ASSERT_NE(nullptr, server_context_config->certificateValidationContext());
  EXPECT_EQ("unnamed_ca_cert_" + expected_hash,
            server_context_config->certificateValidationContext()->caCertName());
}

class CertificateExpirationMetricsTest : public SslCertsTest {
public:
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
};

TEST_F(CertificateExpirationMetricsTest, ServerCertificateExpirationMetrics) {
  const std::string yaml = R"EOF(
common_tls_context:
  tls_certificates:
    certificate_chain:
      filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
    private_key:
      filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);

  Stats::TestUtil::TestStore store;
  auto server_context_config =
      *ServerContextConfigImpl::create(tls_context, factory_context_, {}, false);

  auto tls_certs = server_context_config->tlsCertificates();
  ASSERT_EQ(1, tls_certs.size());
  std::string actual_cert_name = tls_certs[0].get().certificateName();

  absl::Status creation_status = absl::OkStatus();
  TestContextImpl context(*store.rootScope(), *server_context_config, server_factory_context_,
                          creation_status);
  ASSERT_OK(creation_status);

  std::string expected_metric_name =
      absl::StrCat("ssl.certificate.", actual_cert_name, ".expiration_unix_time_seconds");

  auto gauge_opt = store.findGaugeByString(expected_metric_name);
  EXPECT_TRUE(gauge_opt.has_value());
  EXPECT_EQ(gauge_opt->get().value(), 1787339648);
}

TEST_F(CertificateExpirationMetricsTest, ClientCertificateExpirationMetrics) {
  const std::string yaml = R"EOF(
common_tls_context:
  tls_certificates:
    certificate_chain:
      filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
    private_key:
      filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
)EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);

  Stats::TestUtil::TestStore store;
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context_);

  auto tls_certs = client_context_config->tlsCertificates();
  ASSERT_EQ(1, tls_certs.size());
  std::string actual_cert_name = tls_certs[0].get().certificateName();

  absl::Status creation_status = absl::OkStatus();
  TestContextImpl context(*store.rootScope(), *client_context_config, server_factory_context_,
                          creation_status);
  ASSERT_OK(creation_status);

  std::string expected_metric_name =
      absl::StrCat("ssl.certificate.", actual_cert_name, ".expiration_unix_time_seconds");

  auto gauge_opt = store.findGaugeByString(expected_metric_name);
  EXPECT_TRUE(gauge_opt.has_value());
  EXPECT_EQ(gauge_opt->get().value(), 1787339648);
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
