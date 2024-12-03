#include <cstdint>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#include "envoy/common/exception.h"

#include "source/common/common/c_smart_ptr.h"
#include "source/common/event/real_time_system.h"
#include "source/common/tls/stats.h"
#include "source/extensions/transport_sockets/tls/cert_validator/spiffe/spiffe_validator.h"

#include "test/common/tls/cert_validator/test_common.h"
#include "test/common/tls/ssl_test_utility.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

using TestCertificateValidationContextConfigPtr =
    std::unique_ptr<TestCertificateValidationContextConfig>;
using SPIFFEValidatorPtr = std::unique_ptr<SPIFFEValidator>;
using ASN1IA5StringPtr = CSmartPtr<ASN1_IA5STRING, ASN1_IA5STRING_free>;
using GeneralNamesPtr = CSmartPtr<GENERAL_NAMES, GENERAL_NAMES_free>;
using X509StoreContextPtr = CSmartPtr<X509_STORE_CTX, X509_STORE_CTX_free>;
using X509Ptr = CSmartPtr<X509, X509_free>;
using SSLContextPtr = CSmartPtr<SSL_CTX, SSL_CTX_free>;

class TestSPIFFEValidator : public testing::Test {
public:
  TestSPIFFEValidator() : stats_(generateSslStats(*store_.rootScope())) {}
  void initialize(std::string yaml, TimeSource& time_source) {
    envoy::config::core::v3::TypedExtensionConfig typed_conf;
    TestUtility::loadFromYaml(yaml, typed_conf);
    config_ = std::make_unique<TestCertificateValidationContextConfig>(
        typed_conf, allow_expired_certificate_, san_matchers_);
    ON_CALL(factory_context_, timeSource()).WillByDefault(testing::ReturnRef(time_source));
    validator_ = std::make_unique<SPIFFEValidator>(config_.get(), stats_, factory_context_);
  }

  void initialize(std::string yaml) {
    envoy::config::core::v3::TypedExtensionConfig typed_conf;
    TestUtility::loadFromYaml(yaml, typed_conf);
    config_ = std::make_unique<TestCertificateValidationContextConfig>(
        typed_conf, allow_expired_certificate_, san_matchers_);
    validator_ = std::make_unique<SPIFFEValidator>(config_.get(), stats_, factory_context_);
  };

  void initialize() { validator_ = std::make_unique<SPIFFEValidator>(stats_, time_system_); }

  // Getter.
  SPIFFEValidator& validator() { return *validator_; }
  SslStats& stats() { return stats_; }

  // Setter.
  void setAllowExpiredCertificate(bool val) { allow_expired_certificate_ = val; }
  void setSanMatchers(std::vector<envoy::type::matcher::v3::StringMatcher> san_matchers) {
    san_matchers_.clear();
    for (auto& matcher : san_matchers) {
      san_matchers_.emplace_back();
      san_matchers_.back().set_san_type(
          envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::DNS);
      *san_matchers_.back().mutable_matcher() = matcher;

      san_matchers_.emplace_back();
      san_matchers_.back().set_san_type(
          envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::URI);
      *san_matchers_.back().mutable_matcher() = matcher;

      san_matchers_.emplace_back();
      san_matchers_.back().set_san_type(
          envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::EMAIL);
      *san_matchers_.back().mutable_matcher() = matcher;

      san_matchers_.emplace_back();
      san_matchers_.back().set_san_type(
          envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::IP_ADDRESS);
      *san_matchers_.back().mutable_matcher() = matcher;
    }
  };

private:
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  bool allow_expired_certificate_{false};
  TestCertificateValidationContextConfigPtr config_;
  std::vector<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher> san_matchers_{};
  Stats::TestUtil::TestStore store_;
  SslStats stats_;
  Event::TestRealTimeSystem time_system_;
  SPIFFEValidatorPtr validator_;
};

TEST_F(TestSPIFFEValidator, InvalidCA) {
  // Invalid trust bundle.
  EXPECT_THROW_WITH_MESSAGE(initialize(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: hello.com
      trust_bundle:
        inline_string: "invalid"
  )EOF")),
                            EnvoyException, "Failed to load trusted CA certificate for hello.com");
}

// Multiple trust bundles are given for the same trust domain.
TEST_F(TestSPIFFEValidator, Constructor) {
  EXPECT_THROW_WITH_MESSAGE(initialize(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: hello.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert_with_crl.pem"
    - name: hello.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert_with_crl.pem"
  )EOF")),
                            EnvoyException,
                            "Multiple trust bundles are given for one trust domain for hello.com");

  // Single trust bundle.
  initialize(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: hello.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert_with_crl.pem"
  )EOF"));

  EXPECT_EQ(1, validator().trustBundleStores().size());
  EXPECT_NE(validator().getCaFileName().find("test_data/ca_cert_with_crl.pem"), std::string::npos);
  EXPECT_NE(validator().getCaFileName().find("hello.com"), std::string::npos);

  // Multiple trust bundles.
  initialize(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: hello.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
    - name: k8s-west.example.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/keyusage_crl_sign_cert.pem"
  )EOF"));

  EXPECT_EQ(2, validator().trustBundleStores().size());
}

TEST(SPIFFEValidator, TestExtractTrustDomain) {
  EXPECT_EQ("", SPIFFEValidator::extractTrustDomain("foo"));
  EXPECT_EQ("", SPIFFEValidator::extractTrustDomain("abc.com/"));
  EXPECT_EQ("", SPIFFEValidator::extractTrustDomain("abc.com/workload/"));
  EXPECT_EQ("", SPIFFEValidator::extractTrustDomain("spiffe://"));
  EXPECT_EQ("abc.com", SPIFFEValidator::extractTrustDomain("spiffe://abc.com/"));
  EXPECT_EQ("dev.envoy.com",
            SPIFFEValidator::extractTrustDomain("spiffe://dev.envoy.com/workload1"));
  EXPECT_EQ("k8s-west.example.com", SPIFFEValidator::extractTrustDomain(
                                        "spiffe://k8s-west.example.com/ns/staging/sa/default"));
}

TEST(SPIFFEValidator, TestCertificatePrecheck) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      // basicConstraints: CA:True,
      "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));
  EXPECT_FALSE(SPIFFEValidator::certificatePrecheck(cert.get()));

  cert = readCertFromFile(TestEnvironment::substitute(
      // basicConstraints CA:False, keyUsage has keyCertSign
      "{{ test_rundir "
      "}}/test/common/tls/test_data/keyusage_cert_sign_cert.pem"));
  EXPECT_FALSE(SPIFFEValidator::certificatePrecheck(cert.get()));

  cert = readCertFromFile(TestEnvironment::substitute(
      // basicConstraints CA:False, keyUsage has cRLSign
      "{{ test_rundir "
      "}}/test/common/tls/test_data/keyusage_crl_sign_cert.pem"));
  EXPECT_FALSE(SPIFFEValidator::certificatePrecheck(cert.get()));

  cert = readCertFromFile(TestEnvironment::substitute(
      // basicConstraints CA:False, keyUsage does not have keyCertSign and cRLSign
      // should be considered valid (i.e. return 1).
      "{{ test_rundir }}/test/common/tls/test_data/extensions_cert.pem"));
  EXPECT_TRUE(SPIFFEValidator::certificatePrecheck(cert.get()));
}

TEST_F(TestSPIFFEValidator, TestInitializeSslContexts) {
  initialize();
  EXPECT_EQ(SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
            validator().initializeSslContexts({}, false).value());
}

TEST_F(TestSPIFFEValidator, TestGetTrustBundleStore) {
  initialize();

  // No SAN
  auto cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/extensions_cert.pem"));
  EXPECT_FALSE(validator().getTrustBundleStore(cert.get()));

  // Non-SPIFFE SAN
  cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/non_spiffe_san_cert.pem"));
  EXPECT_FALSE(validator().getTrustBundleStore(cert.get()));

  // SPIFFE SAN
  cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/spiffe_san_cert.pem"));

  // Trust bundle not provided.
  EXPECT_FALSE(validator().getTrustBundleStore(cert.get()));

  // Trust bundle provided.
  validator().trustBundleStores().emplace("example.com", X509StorePtr(X509_STORE_new()));
  EXPECT_TRUE(validator().getTrustBundleStore(cert.get()));
}

TEST_F(TestSPIFFEValidator, TestDoVerifyCertChainWithEmptyChain) {
  initialize();
  TestSslExtendedSocketInfo info;
  SSLContextPtr ssl_ctx = SSL_CTX_new(TLS_method());
  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  ValidationResults results =
      validator().doVerifyCertChain(*cert_chain, info.createValidateResultCallback(),
                                    /*transport_socket_options=*/nullptr, *ssl_ctx, {}, false, "");
  EXPECT_EQ(ValidationResults::ValidationStatus::Failed, results.status);
  EXPECT_EQ(Envoy::Ssl::ClientValidationStatus::NotValidated, results.detailed_status);
  EXPECT_EQ(1, stats().fail_verify_error_.value());
}

TEST_F(TestSPIFFEValidator, TestDoVerifyCertChainPrecheckFailure) {
  initialize();
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      // basicConstraints: CA:True
      "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));
  TestSslExtendedSocketInfo info;
  SSLContextPtr ssl_ctx = SSL_CTX_new(TLS_method());
  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  sk_X509_push(cert_chain.get(), cert.release());
  ValidationResults results =
      validator().doVerifyCertChain(*cert_chain, info.createValidateResultCallback(),
                                    /*transport_socket_options=*/nullptr, *ssl_ctx, {}, false, "");
  EXPECT_EQ(ValidationResults::ValidationStatus::Failed, results.status);
  EXPECT_EQ(Envoy::Ssl::ClientValidationStatus::Failed, results.detailed_status);
  EXPECT_EQ(1, stats().fail_verify_error_.value());
}

TEST_F(TestSPIFFEValidator, TestDoVerifyCertChainSingleTrustDomain) {
  initialize(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  )EOF"));

  X509StorePtr store = X509_STORE_new();
  SSLContextPtr ssl_ctx = SSL_CTX_new(TLS_method());
  TestSslExtendedSocketInfo info;
  {
    // Trust domain matches so should be accepted.
    auto cert = readCertFromFile(TestEnvironment::substitute(
        "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"));
    bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
    sk_X509_push(cert_chain.get(), cert.release());
    EXPECT_EQ(ValidationResults::ValidationStatus::Successful,
              validator()
                  .doVerifyCertChain(*cert_chain, info.createValidateResultCallback(),
                                     /*transport_socket_options=*/nullptr, *ssl_ctx, {}, false, "")
                  .status);
  }

  {
    // Different trust domain so should be rejected.
    auto cert = readCertFromFile(TestEnvironment::substitute(
        "{{ test_rundir }}/test/common/tls/test_data/spiffe_san_cert.pem"));
    bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
    sk_X509_push(cert_chain.get(), cert.release());
    EXPECT_EQ(ValidationResults::ValidationStatus::Failed,
              validator()
                  .doVerifyCertChain(*cert_chain, info.createValidateResultCallback(),
                                     /*transport_socket_options=*/nullptr, *ssl_ctx, {}, false, "")
                  .status);
  }
  {
    // Does not have san.
    auto cert = readCertFromFile(TestEnvironment::substitute(
        "{{ test_rundir }}/test/common/tls/test_data/extensions_cert.pem"));
    bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
    sk_X509_push(cert_chain.get(), cert.release());
    EXPECT_EQ(ValidationResults::ValidationStatus::Failed,
              validator()
                  .doVerifyCertChain(*cert_chain, info.createValidateResultCallback(),
                                     /*transport_socket_options=*/nullptr, *ssl_ctx, {}, false, "")
                  .status);
  }

  EXPECT_EQ(2, stats().fail_verify_error_.value());
}

TEST_F(TestSPIFFEValidator, TestDoVerifyCertChainMultipleTrustDomain) {
  initialize(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
    - name: example.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  )EOF"));

  X509StorePtr store = X509_STORE_new();
  SSLContextPtr ssl_ctx = SSL_CTX_new(TLS_method());
  TestSslExtendedSocketInfo info;

  {
    // Trust domain matches so should be accepted.
    auto cert = readCertFromFile(TestEnvironment::substitute(
        "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"));
    bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
    sk_X509_push(cert_chain.get(), cert.release());
    EXPECT_EQ(ValidationResults::ValidationStatus::Successful,
              validator()
                  .doVerifyCertChain(*cert_chain, info.createValidateResultCallback(),
                                     /*transport_socket_options=*/nullptr, *ssl_ctx, {}, false, "")
                  .status);
  }

  {
    auto cert = readCertFromFile(TestEnvironment::substitute(
        "{{ test_rundir }}/test/common/tls/test_data/spiffe_san_cert.pem"));
    bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
    sk_X509_push(cert_chain.get(), cert.release());
    EXPECT_EQ(ValidationResults::ValidationStatus::Successful,
              validator()
                  .doVerifyCertChain(*cert_chain, info.createValidateResultCallback(),
                                     /*transport_socket_options=*/nullptr, *ssl_ctx, {}, false, "")
                  .status);
  }

  {
    // Trust domain matches but it has expired.
    auto cert = readCertFromFile(
        TestEnvironment::substitute("{{ test_rundir "
                                    "}}/test/common/tls/test_data/expired_spiffe_san_cert.pem"));
    bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
    sk_X509_push(cert_chain.get(), cert.release());
    EXPECT_EQ(ValidationResults::ValidationStatus::Failed,
              validator()
                  .doVerifyCertChain(*cert_chain, info.createValidateResultCallback(),
                                     /*transport_socket_options=*/nullptr, *ssl_ctx, {}, false, "")
                  .status);
  }

  {
    // Does not have san.
    auto cert = readCertFromFile(TestEnvironment::substitute(
        "{{ test_rundir }}/test/common/tls/test_data/extensions_cert.pem"));
    bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
    sk_X509_push(cert_chain.get(), cert.release());
    EXPECT_EQ(ValidationResults::ValidationStatus::Failed,
              validator()
                  .doVerifyCertChain(*cert_chain, info.createValidateResultCallback(),
                                     /*transport_socket_options=*/nullptr, *ssl_ctx, {}, false, "")
                  .status);
  }

  EXPECT_EQ(2, stats().fail_verify_error_.value());
}

TEST_F(TestSPIFFEValidator, TestDoVerifyCertChainMultipleTrustDomainAllowExpired) {
  setAllowExpiredCertificate(true);
  initialize(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: example.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  )EOF"));

  X509StorePtr store = X509_STORE_new();
  SSLContextPtr ssl_ctx = SSL_CTX_new(TLS_method());
  TestSslExtendedSocketInfo info;
  // Trust domain matches and it has expired but allow_expired_certificate is true, so this
  // should be accepted.
  auto cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/expired_spiffe_san_cert.pem"));
  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  sk_X509_push(cert_chain.get(), cert.release());
  EXPECT_EQ(ValidationResults::ValidationStatus::Successful,
            validator()
                .doVerifyCertChain(*cert_chain, info.createValidateResultCallback(),
                                   /*transport_socket_options=*/nullptr, *ssl_ctx, {}, false, "")
                .status);

  EXPECT_EQ(0, stats().fail_verify_error_.value());
}

TEST_F(TestSPIFFEValidator, TestDoVerifyCertChainSANMatching) {
  const auto config = TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  )EOF");

  X509StorePtr store = X509_STORE_new();
  SSLContextPtr ssl_ctx = SSL_CTX_new(TLS_method());
  // URI SAN = spiffe://lyft.com/test-team
  auto cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"));
  X509StoreContextPtr store_ctx = X509_STORE_CTX_new();
  EXPECT_TRUE(X509_STORE_CTX_init(store_ctx.get(), store.get(), cert.get(), nullptr));
  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  sk_X509_push(cert_chain.get(), cert.release());
  TestSslExtendedSocketInfo info;
  info.setCertificateValidationStatus(Envoy::Ssl::ClientValidationStatus::NotValidated);
  {
    envoy::type::matcher::v3::StringMatcher matcher;
    matcher.set_prefix("spiffe://lyft.com/");
    setSanMatchers({matcher});
    initialize(config);
    ValidationResults results = validator().doVerifyCertChain(
        *cert_chain, info.createValidateResultCallback(),
        /*transport_socket_options=*/nullptr, *ssl_ctx, {}, false, "");
    EXPECT_EQ(ValidationResults::ValidationStatus::Successful, results.status);
    EXPECT_EQ(Envoy::Ssl::ClientValidationStatus::Validated, results.detailed_status);
  }
  {
    envoy::type::matcher::v3::StringMatcher matcher;
    matcher.set_prefix("spiffe://example.com/");
    setSanMatchers({matcher});
    initialize(config);
    ValidationResults results = validator().doVerifyCertChain(
        *cert_chain, info.createValidateResultCallback(),
        /*transport_socket_options=*/nullptr, *ssl_ctx, {}, false, "");
    EXPECT_EQ(ValidationResults::ValidationStatus::Failed, results.status);
    EXPECT_EQ(Envoy::Ssl::ClientValidationStatus::Failed, results.detailed_status);
    EXPECT_EQ(1, stats().fail_verify_san_.value());
    stats().fail_verify_san_.reset();
  }
}

TEST_F(TestSPIFFEValidator, TestDoVerifyCertChainIntermediateCerts) {
  initialize(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: example.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  )EOF"));

  TestSslExtendedSocketInfo info;
  // Chain contains workload, intermediate, and ca cert, so it should be accepted.
  auto cert =
      readCertFromFile(TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/"
                                                   "spiffe_san_signed_by_intermediate_cert.pem"));
  auto intermediate_ca_cert =
      readCertFromFile(TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/"
                                                   "intermediate_ca_cert.pem"));

  SSLContextPtr ssl_ctx = SSL_CTX_new(TLS_method());
  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  sk_X509_push(cert_chain.get(), cert.release());
  sk_X509_push(cert_chain.get(), intermediate_ca_cert.release());
  EXPECT_EQ(ValidationResults::ValidationStatus::Successful,
            validator()
                .doVerifyCertChain(*cert_chain, info.createValidateResultCallback(),
                                   /*transport_socket_options=*/nullptr, *ssl_ctx, {}, false, "")
                .status);
}

void addIA5StringGenNameExt(X509* cert, int type, const std::string name) {
  GeneralNamesPtr gens = sk_GENERAL_NAME_new_null();
  GENERAL_NAME* gen = GENERAL_NAME_new(); // ownership taken by "gens"
  ASN1IA5StringPtr ia5 = ASN1_IA5STRING_new();
  EXPECT_TRUE(ASN1_STRING_set(ia5.get(), name.data(), name.length()));
  GENERAL_NAME_set0_value(gen, type, ia5.release());
  sk_GENERAL_NAME_push(gens.get(), gen);
  EXPECT_TRUE(X509_add1_ext_i2d(cert, NID_subject_alt_name, gens.get(), 0, X509V3_ADD_DEFAULT));
}

TEST_F(TestSPIFFEValidator, TestMatchSubjectAltNameWithURISan) {
  envoy::type::matcher::v3::StringMatcher exact_matcher, prefix_matcher, regex_matcher;
  exact_matcher.set_exact("spiffe://example.com/workload");
  prefix_matcher.set_prefix("spiffe://envoy.com");
  regex_matcher.mutable_safe_regex()->mutable_google_re2();
  regex_matcher.mutable_safe_regex()->set_regex("spiffe:\\/\\/([a-z]+)\\.myorg\\.com\\/.+");
  setSanMatchers({exact_matcher, prefix_matcher, regex_matcher});
  initialize(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  )EOF"));

  {
    X509Ptr leaf = X509_new();
    addIA5StringGenNameExt(leaf.get(), GEN_URI, "spiffe://envoy.com/myapp");
    EXPECT_TRUE(validator().matchSubjectAltName(*leaf.get()));
  }
  {
    X509Ptr leaf = X509_new();
    addIA5StringGenNameExt(leaf.get(), GEN_URI, "spiffe://example.com/workload");
    EXPECT_TRUE(validator().matchSubjectAltName(*leaf.get()));
  }
  {
    X509Ptr leaf = X509_new();
    addIA5StringGenNameExt(leaf.get(), GEN_URI, "spiffe://example.com/otherworkload");
    EXPECT_FALSE(validator().matchSubjectAltName(*leaf.get()));
  }
  {
    X509Ptr leaf = X509_new();
    addIA5StringGenNameExt(leaf.get(), GEN_URI, "spiffe://foo.myorg.com/workload");
    EXPECT_TRUE(validator().matchSubjectAltName(*leaf.get()));
  }
  {
    X509Ptr leaf = X509_new();
    addIA5StringGenNameExt(leaf.get(), GEN_URI, "spiffe://bar.myorg.com/workload");
    EXPECT_TRUE(validator().matchSubjectAltName(*leaf.get()));
  }
}

// SPIFFE validator ignores any SANs other than URI.
TEST_F(TestSPIFFEValidator, TestMatchSubjectAltNameWithoutURISan) {
  envoy::type::matcher::v3::StringMatcher exact_matcher, prefix_matcher;
  exact_matcher.set_exact("spiffe://example.com/workload");
  prefix_matcher.set_prefix("envoy");
  setSanMatchers({exact_matcher, prefix_matcher});
  initialize(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  )EOF"));

  {
    X509Ptr leaf = X509_new();
    addIA5StringGenNameExt(leaf.get(), GEN_DNS, "envoy.com/workload");
    EXPECT_FALSE(validator().matchSubjectAltName(*leaf.get()));
  }
  {
    X509Ptr leaf = X509_new();
    addIA5StringGenNameExt(leaf.get(), GEN_DNS, "spiffe://example.com/workload");
    EXPECT_FALSE(validator().matchSubjectAltName(*leaf.get()));
  }
  {
    X509Ptr leaf = X509_new();
    addIA5StringGenNameExt(leaf.get(), GEN_EMAIL, "envoy@example.co.jp");
    EXPECT_FALSE(validator().matchSubjectAltName(*leaf.get()));
  }
}

TEST_F(TestSPIFFEValidator, TestGetCaCertInformation) {
  initialize();

  // No cert is set so this should be nullptr.
  EXPECT_FALSE(validator().getCaCertInformation());

  initialize(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/spiffe_san_cert.pem"
    - name: example.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  )EOF"));

  auto actual = validator().getCaCertInformation();
  EXPECT_TRUE(actual);
}

TEST_F(TestSPIFFEValidator, TestDaysUntilFirstCertExpires) {
  initialize();
  EXPECT_EQ(std::numeric_limits<uint32_t>::max(), validator().daysUntilFirstCertExpires().value());

  Event::SimulatedTimeSystem time_system;
  time_system.setSystemTime(std::chrono::milliseconds(0));

  initialize(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/spiffe_san_cert.pem"
    - name: example.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/intermediate_ca_cert.pem"
  )EOF"),
             time_system);
  EXPECT_EQ(20686, validator().daysUntilFirstCertExpires().value());
  time_system.setSystemTime(std::chrono::milliseconds(864000000));
  EXPECT_EQ(20676, validator().daysUntilFirstCertExpires().value());
}

TEST_F(TestSPIFFEValidator, TestDaysUntilFirstCertExpiresExpired) {
  Event::SimulatedTimeSystem time_system;
  // 2033-05-18 03:33:20 UTC
  const time_t known_date_time = 2000000000;
  time_system.setSystemTime(std::chrono::system_clock::from_time_t(known_date_time));

  initialize(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: example.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/spiffe_san_cert.pem"
  )EOF"),
             time_system);

  EXPECT_EQ(absl::nullopt, validator().daysUntilFirstCertExpires());
}

TEST_F(TestSPIFFEValidator, TestAddClientValidationContext) {
  Event::TestRealTimeSystem time_system;
  initialize(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/spiffe_san_cert.pem"
    - name: example.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
    - name: foo.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  )EOF"),
             time_system);

  bool foundTestServer = false;
  bool foundTestCA = false;
  SSLContextPtr ctx = SSL_CTX_new(TLS_method());
  ASSERT_TRUE(validator().addClientValidationContext(ctx.get(), false).ok());
  for (X509_NAME* name : SSL_CTX_get_client_CA_list(ctx.get())) {
    const int cn_index = X509_NAME_get_index_by_NID(name, NID_commonName, -1);
    EXPECT_TRUE(cn_index >= 0);
    X509_NAME_ENTRY* cn_entry = X509_NAME_get_entry(name, cn_index);
    EXPECT_TRUE(cn_entry);
    ASN1_STRING* cn_asn1 = X509_NAME_ENTRY_get_data(cn_entry);
    EXPECT_TRUE(cn_asn1);

    auto cn_str = std::string(reinterpret_cast<char const*>(ASN1_STRING_data(cn_asn1)));
    if (cn_str == "Test Server") {
      foundTestServer = true;
    } else if (cn_str == "Test CA") {
      foundTestCA = true;
    }
  }

  EXPECT_TRUE(foundTestServer);
  EXPECT_TRUE(foundTestCA);
}

TEST_F(TestSPIFFEValidator, TestUpdateDigestForSessionId) {
  Event::TestRealTimeSystem time_system;
  initialize(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/spiffe_san_cert.pem"
    - name: example.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  )EOF"),
             time_system);
  uint8_t hash_buffer[EVP_MAX_MD_SIZE];
  bssl::ScopedEVP_MD_CTX md;
  EVP_DigestInit(md.get(), EVP_sha256());
  validator().updateDigestForSessionId(md, hash_buffer, SHA256_DIGEST_LENGTH);
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
