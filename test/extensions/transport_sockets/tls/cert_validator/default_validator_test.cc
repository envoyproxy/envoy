#include <string>
#include <vector>

#include "source/extensions/transport_sockets/tls/cert_validator/default_validator.h"
#include "source/extensions/transport_sockets/tls/cert_validator/san_matcher.h"

#include "test/extensions/transport_sockets/tls/ssl_test_utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

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
  EXPECT_EQ(default_validator->verifyCertificate(cert.get(), /*verify_san_list=*/{}, san_matchers),
            Envoy::Ssl::ClientValidationStatus::Validated);
  EXPECT_EQ(stats.fail_verify_san_.value(), 0);

  matcher.MergeFrom(TestUtility::createExactMatcher("hello.example.com"));
  std::vector<SanMatcherPtr> invalid_san_matchers;
  invalid_san_matchers.push_back(
      SanMatcherPtr{std::make_unique<StringSanMatcher>(GEN_DNS, matcher)});
  // Verify the certificate with incorrect SAN exact matcher.
  EXPECT_EQ(default_validator->verifyCertificate(cert.get(), /*verify_san_list=*/{},
                                                 invalid_san_matchers),
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
                                                 /*subject_alt_name_matchers=*/{}),
            Envoy::Ssl::ClientValidationStatus::NotValidated);
  bssl::UniquePtr<X509> cert(X509_new());
  EXPECT_EQ(default_validator->doVerifyCertChain(/*store_ctx=*/nullptr,
                                                 /*ssl_extended_info=*/nullptr,
                                                 /*leaf_cert=*/*cert,
                                                 /*transport_socket_options=*/nullptr),
            0);
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

class MockCertificateValidationContextConfig : public Ssl::CertificateValidationContextConfig {
public:
  MockCertificateValidationContextConfig() {};
  const std::string& caCert() const override {return std::move(std::string());} 
  const std::string& caCertPath() const override {return std::move(std::string());}
  const std::string& certificateRevocationList() const override {return std::move(std::string());}
  const std::string& certificateRevocationListPath() const override {return std::move(std::string());}
  const std::vector<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher>& subjectAltNameMatchers() const override {
    auto matcher = envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher();
    matcher.set_san_type(static_cast<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher_SanType>(123));
    std::vector<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher> matchers = {matcher};
    return std::move(matchers);
  }
  const std::vector<std::string>& verifyCertificateHashList() const override {return std::move(std::vector<std::string>());}
  const std::vector<std::string>& verifyCertificateSpkiList() const override {return std::move(std::vector<std::string>());}
  bool allowExpiredCertificate() const override {return false;}
  MOCK_METHOD(envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext::TrustChainVerification, trustChainVerification, (), (const override));
  MOCK_METHOD(const absl::optional<envoy::config::core::v3::TypedExtensionConfig>&, customValidatorConfig, (), (const override));
  MOCK_METHOD(Api::Api&, api, (), (const override));
  bool onlyVerifyLeafCertificateCrl() const override { return false; }
};

TEST(DefaultCertValidatorTest, TestUnexpectedSanMatcherType) {
    auto mock_context_config = std::make_unique<MockCertificateValidationContextConfig>();
    EXPECT_CALL(*mock_context_config.get(), trustChainVerification()).WillRepeatedly(testing::Return(
        envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext::ACCEPT_UNTRUSTED));
    auto matchers = std::vector<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher>();
    Stats::TestUtil::TestStore store;
    auto ssl_stats = generateSslStats(store);
    auto validator = std::make_unique<DefaultCertValidator>(mock_context_config.get(), ssl_stats, Event::GlobalTimeSystem().timeSystem()); 
    auto ctx = std::vector<SSL_CTX*>();
    EXPECT_THROW_WITH_REGEX(validator->initializeSslContexts(ctx, false), EnvoyException, "Failed to create string SAN matcher of type.*");
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
