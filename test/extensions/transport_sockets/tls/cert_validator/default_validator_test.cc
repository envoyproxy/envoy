#include <string>
#include <vector>

#include "source/extensions/transport_sockets/tls/cert_validator/default_validator.h"
#include "source/extensions/transport_sockets/tls/cert_validator/san_matcher.h"

#include "test/extensions/transport_sockets/tls/cert_validator/test_common.h"
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

using TestCertificateValidationContextConfigPtr =
    std::unique_ptr<TestCertificateValidationContextConfig>;
using X509StoreContextPtr = CSmartPtr<X509_STORE_CTX, X509_STORE_CTX_free>;
using X509StorePtr = CSmartPtr<X509_STORE, X509_STORE_free>;
using SSLContextPtr = CSmartPtr<SSL_CTX, SSL_CTX_free>;

TEST(DefaultCertValidatorTest, TestVerifySubjectAltNameDNSMatched) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  std::vector<std::string> verify_subject_alt_name_list = {"server1.example.com",
                                                           "server2.example.com"};
  EXPECT_TRUE(DefaultCertValidator::verifySubjectAltName(cert.get(), verify_subject_alt_name_list));
}

TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameDNSMatched) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.MergeFrom(TestUtility::createRegexMatcher(R"raw([^.]*\.example.com)raw"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_DNS, matcher)});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameIncorrectTypeMatched) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.MergeFrom(TestUtility::createRegexMatcher(R"raw([^.]*\.example.com)raw"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_URI, matcher)});
  EXPECT_FALSE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameWildcardDNSMatched) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_exact("api.example.com");
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_DNS, matcher)});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST(DefaultCertValidatorTest, TestMultiLevelMatch) {
  // san_multiple_dns_cert matches *.example.com
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_exact("foo.api.example.com");
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_DNS, matcher)});
  EXPECT_FALSE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST(DefaultCertValidatorTest, TestVerifySubjectAltNameURIMatched) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"));
  std::vector<std::string> verify_subject_alt_name_list = {"spiffe://lyft.com/fake-team",
                                                           "spiffe://lyft.com/test-team"};
  EXPECT_TRUE(DefaultCertValidator::verifySubjectAltName(cert.get(), verify_subject_alt_name_list));
}

TEST(DefaultCertValidatorTest, TestVerifySubjectAltMultiDomain) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem"));
  std::vector<std::string> verify_subject_alt_name_list = {"https://a.www.example.com"};
  EXPECT_FALSE(
      DefaultCertValidator::verifySubjectAltName(cert.get(), verify_subject_alt_name_list));
}

TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameURIMatched) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.MergeFrom(TestUtility::createRegexMatcher(R"raw(spiffe://lyft.com/[^/]*-team)raw"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_URI, matcher)});
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST(DefaultCertValidatorTest, TestVerifySubjectAltNameNotMatched) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  std::vector<std::string> verify_subject_alt_name_list = {"foo", "bar"};
  EXPECT_FALSE(
      DefaultCertValidator::verifySubjectAltName(cert.get(), verify_subject_alt_name_list));
}

TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameNotMatched) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.MergeFrom(TestUtility::createRegexMatcher(R"raw([^.]*\.example\.net)raw"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_DNS, matcher)});
  subject_alt_name_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_IPADD, matcher)});
  subject_alt_name_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_URI, matcher)});
  subject_alt_name_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_EMAIL, matcher)});
  EXPECT_FALSE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST(DefaultCertValidatorTest, TestCertificateVerificationWithSANMatcher) {
  Stats::TestUtil::TestStore test_store;
  SslStats stats = generateSslStats(test_store);
  // Create the default validator object.
  auto default_validator =
      std::make_unique<Extensions::TransportSockets::Tls::DefaultCertValidator>(
          /*CertificateValidationContextConfig=*/nullptr, stats,
          Event::GlobalTimeSystem().timeSystem());

  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.MergeFrom(TestUtility::createRegexMatcher(R"raw([^.]*\.example.com)raw"));
  std::vector<SanMatcherPtr> san_matchers;
  san_matchers.push_back(SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_DNS, matcher)});
  // Verify the certificate with correct SAN regex matcher.
  EXPECT_EQ(default_validator->verifyCertificate(cert.get(), /*verify_san_list=*/{}, san_matchers,
                                                 nullptr, nullptr),
            Envoy::Ssl::ClientValidationStatus::Validated);
  EXPECT_EQ(stats.fail_verify_san_.value(), 0);

  matcher.MergeFrom(TestUtility::createExactMatcher("hello.example.com"));
  std::vector<SanMatcherPtr> invalid_san_matchers;
  invalid_san_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_DNS, matcher)});
  std::string error;
  // Verify the certificate with incorrect SAN exact matcher.
  EXPECT_EQ(default_validator->verifyCertificate(cert.get(), /*verify_san_list=*/{},
                                                 invalid_san_matchers, &error, nullptr),
            Envoy::Ssl::ClientValidationStatus::Failed);
  EXPECT_EQ(stats.fail_verify_san_.value(), 1);
}

TEST(DefaultCertValidatorTest, TestCertificateVerificationWithNoValidationContext) {
  Stats::TestUtil::TestStore test_store;
  SslStats stats = generateSslStats(test_store);
  // Create the default validator object.
  auto default_validator =
      std::make_unique<Extensions::TransportSockets::Tls::DefaultCertValidator>(
          /*CertificateValidationContextConfig=*/nullptr, stats,
          Event::GlobalTimeSystem().timeSystem());

  EXPECT_EQ(default_validator->verifyCertificate(/*cert=*/nullptr, /*verify_san_list=*/{},
                                                 /*subject_alt_name_matchers=*/{}, nullptr,
                                                 nullptr),
            Envoy::Ssl::ClientValidationStatus::NotValidated);
  bssl::UniquePtr<X509> cert(X509_new());
  bssl::UniquePtr<X509_STORE_CTX> store_ctx(X509_STORE_CTX_new());
  EXPECT_EQ(default_validator->doSynchronousVerifyCertChain(/*store_ctx=*/store_ctx.get(),
                                                            /*ssl_extended_info=*/nullptr,
                                                            /*leaf_cert=*/*cert,
                                                            /*transport_socket_options=*/nullptr),
            0);
  SSLContextPtr ssl_ctx = SSL_CTX_new(TLS_method());
  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  ASSERT_TRUE(bssl::PushToStack(cert_chain.get(), std::move(cert)));
  EXPECT_EQ(ValidationResults::ValidationStatus::Failed,
            default_validator
                ->doVerifyCertChain(*cert_chain, /*callback=*/nullptr,
                                    /*ssl_extended_info=*/nullptr,
                                    /*transport_socket_options=*/nullptr, *ssl_ctx, {}, false, "")
                .status);
}

TEST(DefaultCertValidatorTest, TestCertificateVerificationWithEmptyCertChain) {
  Stats::TestUtil::TestStore test_store;
  SslStats stats = generateSslStats(test_store);
  // Create the default validator object.
  auto default_validator =
      std::make_unique<Extensions::TransportSockets::Tls::DefaultCertValidator>(
          /*CertificateValidationContextConfig=*/nullptr, stats,
          Event::GlobalTimeSystem().timeSystem());

  SSLContextPtr ssl_ctx = SSL_CTX_new(TLS_method());
  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  TestSslExtendedSocketInfo extended_socket_info;
  EXPECT_EQ(ValidationResults::ValidationStatus::Failed,
            default_validator
                ->doVerifyCertChain(*cert_chain, /*callback=*/nullptr, &extended_socket_info,
                                    /*transport_socket_options=*/nullptr, *ssl_ctx, {}, false, "")
                .status);
  EXPECT_EQ(extended_socket_info.certificateValidationStatus(),
            Ssl::ClientValidationStatus::NotValidated);
}

TEST(DefaultCertValidatorTest, NoSanInCert) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/fake_ca_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.MergeFrom(TestUtility::createRegexMatcher(R"raw([^.]*\.example\.net)raw"));
  std::vector<SanMatcherPtr> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_DNS, matcher)});
  EXPECT_FALSE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST(DefaultCertValidatorTest, WithVerifyDepth) {

  Stats::TestUtil::TestStore test_store;
  SslStats stats = generateSslStats(test_store);
  envoy::config::core::v3::TypedExtensionConfig typed_conf;
  std::vector<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher> san_matchers{};

  bssl::UniquePtr<STACK_OF(X509)> cert_chain = readCertChainFromFile(TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/test_long_cert_chain.pem"));
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/test_random_cert.pem"));
  bssl::UniquePtr<X509> ca_cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"));

  // Create the default validator object.
  // Config includes ca_cert and the verify-depth.
  // Set verify depth < 3, so verification fails. ( There are 3 intermediate certs )

  std::string ca_cert_str(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"));
  TestCertificateValidationContextConfigPtr test_config =
      std::make_unique<TestCertificateValidationContextConfig>(typed_conf, false, san_matchers,
                                                               ca_cert_str, 2);
  auto default_validator =
      std::make_unique<Extensions::TransportSockets::Tls::DefaultCertValidator>(
          test_config.get(), stats, Event::GlobalTimeSystem().timeSystem());

  STACK_OF(X509)* intermediates = cert_chain.get();
  SSLContextPtr ssl_ctx = SSL_CTX_new(TLS_method());
  X509StoreContextPtr store_ctx = X509_STORE_CTX_new();

  X509_STORE* storep = SSL_CTX_get_cert_store(ssl_ctx.get());
  X509_STORE_add_cert(storep, ca_cert.get());
  EXPECT_TRUE(X509_STORE_CTX_init(store_ctx.get(), storep, cert.get(), intermediates));

  default_validator->addClientValidationContext(ssl_ctx.get(), false);
  X509_VERIFY_PARAM_set1(X509_STORE_CTX_get0_param(store_ctx.get()),
                         SSL_CTX_get0_param(ssl_ctx.get()));

  EXPECT_EQ(X509_verify_cert(store_ctx.get()), 0);

  // Now, create config with no depth configuration, verification should pass.
  test_config = std::make_unique<TestCertificateValidationContextConfig>(typed_conf, false,
                                                                         san_matchers, ca_cert_str);
  default_validator = std::make_unique<Extensions::TransportSockets::Tls::DefaultCertValidator>(
      test_config.get(), stats, Event::GlobalTimeSystem().timeSystem());

  // Re-initialize context
  ssl_ctx = SSL_CTX_new(TLS_method());
  store_ctx = X509_STORE_CTX_new();
  storep = SSL_CTX_get_cert_store(ssl_ctx.get());
  X509_STORE_add_cert(storep, ca_cert.get());
  EXPECT_TRUE(X509_STORE_CTX_init(store_ctx.get(), storep, cert.get(), intermediates));

  default_validator->addClientValidationContext(ssl_ctx.get(), false);
  X509_VERIFY_PARAM_set1(X509_STORE_CTX_get0_param(store_ctx.get()),
                         SSL_CTX_get0_param(ssl_ctx.get()));

  EXPECT_EQ(X509_verify_cert(store_ctx.get()), 1);
  EXPECT_EQ(X509_STORE_CTX_get_error(store_ctx.get()), X509_V_OK);
}

class MockCertificateValidationContextConfig : public Ssl::CertificateValidationContextConfig {
public:
  MockCertificateValidationContextConfig() {
    auto matcher = envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher();
    matcher.set_san_type(
        static_cast<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher_SanType>(
            123));
    matchers_.emplace_back(matcher);
  };
  const std::string& caCert() const override { return s_; }
  const std::string& caCertPath() const override { return s_; }
  const std::string& certificateRevocationList() const override { return s_; }
  const std::string& certificateRevocationListPath() const override { return s_; }
  const std::vector<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher>&
  subjectAltNameMatchers() const override {
    return matchers_;
  }
  const std::vector<std::string>& verifyCertificateHashList() const override { return strs_; }
  const std::vector<std::string>& verifyCertificateSpkiList() const override { return strs_; }
  bool allowExpiredCertificate() const override { return false; }
  MOCK_METHOD(envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext::
                  TrustChainVerification,
              trustChainVerification, (), (const override));
  MOCK_METHOD(const absl::optional<envoy::config::core::v3::TypedExtensionConfig>&,
              customValidatorConfig, (), (const override));
  MOCK_METHOD(Api::Api&, api, (), (const override));
  bool onlyVerifyLeafCertificateCrl() const override { return false; }
  absl::optional<uint32_t> maxVerifyDepth() const override { return absl::nullopt; }

private:
  std::string s_;
  std::vector<std::string> strs_;
  std::vector<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher> matchers_;
};

TEST(DefaultCertValidatorTest, TestUnexpectedSanMatcherType) {
  auto mock_context_config = std::make_unique<MockCertificateValidationContextConfig>();
  EXPECT_CALL(*mock_context_config.get(), trustChainVerification())
      .WillRepeatedly(testing::Return(envoy::extensions::transport_sockets::tls::v3::
                                          CertificateValidationContext::ACCEPT_UNTRUSTED));
  auto matchers =
      std::vector<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher>();
  Stats::TestUtil::TestStore store;
  auto ssl_stats = generateSslStats(store);
  auto validator = std::make_unique<DefaultCertValidator>(mock_context_config.get(), ssl_stats,
                                                          Event::GlobalTimeSystem().timeSystem());
  auto ctx = std::vector<SSL_CTX*>();
  EXPECT_THROW_WITH_REGEX(validator->initializeSslContexts(ctx, false), EnvoyException,
                          "Failed to create string SAN matcher of type.*");
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
