#include <string>
#include <vector>

#include "extensions/transport_sockets/tls/cert_validator/default_validator.h"

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

TEST(DefaultCertValidatorTest, TestDnsNameMatching) {
  EXPECT_TRUE(DefaultCertValidator::dnsNameMatch("lyft.com", "lyft.com"));
  EXPECT_TRUE(DefaultCertValidator::dnsNameMatch("a.lyft.com", "*.lyft.com"));
  EXPECT_FALSE(DefaultCertValidator::dnsNameMatch("a.b.lyft.com", "*.lyft.com"));
  EXPECT_FALSE(DefaultCertValidator::dnsNameMatch("foo.test.com", "*.lyft.com"));
  EXPECT_FALSE(DefaultCertValidator::dnsNameMatch("lyft.com", "*.lyft.com"));
  EXPECT_FALSE(DefaultCertValidator::dnsNameMatch("alyft.com", "*.lyft.com"));
  EXPECT_FALSE(DefaultCertValidator::dnsNameMatch("alyft.com", "*lyft.com"));
  EXPECT_FALSE(DefaultCertValidator::dnsNameMatch("lyft.com", "*lyft.com"));
  EXPECT_FALSE(DefaultCertValidator::dnsNameMatch("", "*lyft.com"));
  EXPECT_FALSE(DefaultCertValidator::dnsNameMatch("lyft.com", ""));
}

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
  matcher.set_hidden_envoy_deprecated_regex(".*.example.com");
  std::vector<Matchers::StringMatcherImpl> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(Matchers::StringMatcherImpl(matcher));
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST(DefaultCertValidatorTest, TestMatchSubjectAltNameWildcardDNSMatched) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_exact("api.example.com");
  std::vector<Matchers::StringMatcherImpl> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(Matchers::StringMatcherImpl(matcher));
  EXPECT_TRUE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

TEST(DefaultCertValidatorTest, TestMultiLevelMatch) {
  // san_multiple_dns_cert matches *.example.com
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem"));
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_exact("foo.api.example.com");
  std::vector<Matchers::StringMatcherImpl> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(Matchers::StringMatcherImpl(matcher));
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
  matcher.set_hidden_envoy_deprecated_regex("spiffe://lyft.com/.*-team");
  std::vector<Matchers::StringMatcherImpl> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(Matchers::StringMatcherImpl(matcher));
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
  matcher.set_hidden_envoy_deprecated_regex(".*.foo.com");
  std::vector<Matchers::StringMatcherImpl> subject_alt_name_matchers;
  subject_alt_name_matchers.push_back(Matchers::StringMatcherImpl(matcher));
  EXPECT_FALSE(DefaultCertValidator::matchSubjectAltName(cert.get(), subject_alt_name_matchers));
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
