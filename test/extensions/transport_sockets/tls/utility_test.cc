#include <string>
#include <vector>

#include "source/common/common/c_smart_ptr.h"
#include "source/extensions/transport_sockets/tls/utility.h"

#include "test/extensions/transport_sockets/tls/ssl_test_utility.h"
#include "test/extensions/transport_sockets/tls/test_data/long_validity_cert_info.h"
#include "test/extensions/transport_sockets/tls/test_data/san_dns_cert_info.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace {

using X509StoreContextPtr = CSmartPtr<X509_STORE_CTX, X509_STORE_CTX_free>;
using X509StorePtr = CSmartPtr<X509_STORE, X509_STORE_free>;

TEST(UtilityTest, TestDnsNameMatching) {
  EXPECT_TRUE(Utility::dnsNameMatch("lyft.com", "lyft.com"));
  EXPECT_TRUE(Utility::dnsNameMatch("a.lyft.com", "*.lyft.com"));
  EXPECT_TRUE(Utility::dnsNameMatch("a.LYFT.com", "*.lyft.COM"));
  EXPECT_FALSE(Utility::dnsNameMatch("a.b.lyft.com", "*.lyft.com"));
  EXPECT_FALSE(Utility::dnsNameMatch("foo.test.com", "*.lyft.com"));
  EXPECT_FALSE(Utility::dnsNameMatch("lyft.com", "*.lyft.com"));
  EXPECT_FALSE(Utility::dnsNameMatch("alyft.com", "*.lyft.com"));
  EXPECT_FALSE(Utility::dnsNameMatch("alyft.com", "*lyft.com"));
  EXPECT_FALSE(Utility::dnsNameMatch("lyft.com", "*lyft.com"));
  EXPECT_FALSE(Utility::dnsNameMatch("", "*lyft.com"));
  EXPECT_FALSE(Utility::dnsNameMatch("lyft.com", ""));
}

TEST(UtilityTest, TestGetSubjectAlternateNamesWithDNS) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  const auto& subject_alt_names = Utility::getSubjectAltNames(*cert, GEN_DNS);
  EXPECT_EQ(1, subject_alt_names.size());
}

TEST(UtilityTest, TestMultipleGetSubjectAlternateNamesWithDNS) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem"));
  const auto& subject_alt_names = Utility::getSubjectAltNames(*cert, GEN_DNS);
  EXPECT_EQ(2, subject_alt_names.size());
}

TEST(UtilityTest, TestGetSubjectAlternateNamesWithUri) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"));
  const auto& subject_alt_names = Utility::getSubjectAltNames(*cert, GEN_URI);
  EXPECT_EQ(1, subject_alt_names.size());
}

TEST(UtilityTest, TestGetSubjectAlternateNamesWithEmail) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/spiffe_san_cert.pem"));
  const auto& subject_alt_names = Utility::getSubjectAltNames(*cert, GEN_EMAIL);
  EXPECT_EQ(1, subject_alt_names.size());
  EXPECT_EQ("envoy@example.com", subject_alt_names.front());
}

TEST(UtilityTest, TestGetSubjectAlternateNamesWithNoSAN) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/no_san_cert.pem"));
  const auto& uri_subject_alt_names = Utility::getSubjectAltNames(*cert, GEN_URI);
  EXPECT_EQ(0, uri_subject_alt_names.size());
}

TEST(UtilityTest, TestGetSubject) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  EXPECT_EQ("CN=Test Server,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US",
            Utility::getSubjectFromCertificate(*cert));
}

TEST(UtilityTest, TestGetIssuer) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  EXPECT_EQ("CN=Test CA,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US",
            Utility::getIssuerFromCertificate(*cert));
}

TEST(UtilityTest, TestGetSerialNumber) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  EXPECT_EQ(TEST_SAN_DNS_CERT_SERIAL, Utility::getSerialNumberFromCertificate(*cert));
}

TEST(UtilityTest, TestDaysUntilExpiration) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  // Set a known date (2033-05-18 03:33:20 UTC) so that we get fixed output from this test.
  const time_t known_date_time = 2000000000;
  Event::SimulatedTimeSystem time_source;
  time_source.setSystemTime(std::chrono::system_clock::from_time_t(known_date_time));

  // Get expiration time from the certificate info.
  const absl::Time expiration =
      TestUtility::parseTime(TEST_SAN_DNS_CERT_NOT_AFTER, "%b %e %H:%M:%S %Y GMT");

  int days = std::difftime(absl::ToTimeT(expiration), known_date_time) / (60 * 60 * 24);
  EXPECT_EQ(days, Utility::getDaysUntilExpiration(cert.get(), time_source));
}

TEST(UtilityTest, TestDaysUntilExpirationWithNull) {
  Event::SimulatedTimeSystem time_source;
  EXPECT_EQ(std::numeric_limits<int>::max(), Utility::getDaysUntilExpiration(nullptr, time_source));
}

TEST(UtilityTest, TestValidFrom) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  const std::string formatted =
      TestUtility::formatTime(Utility::getValidFrom(*cert), "%b %e %H:%M:%S %Y GMT");
  EXPECT_EQ(TEST_SAN_DNS_CERT_NOT_BEFORE, formatted);
}

TEST(UtilityTest, TestExpirationTime) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  const std::string formatted =
      TestUtility::formatTime(Utility::getExpirationTime(*cert), "%b %e %H:%M:%S %Y GMT");
  EXPECT_EQ(TEST_SAN_DNS_CERT_NOT_AFTER, formatted);
}

TEST(UtilityTest, TestLongExpirationTime) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/long_validity_cert.pem"));
  const std::string formatted =
      TestUtility::formatTime(Utility::getExpirationTime(*cert), "%b %e %H:%M:%S %Y GMT");
  EXPECT_EQ(TEST_LONG_VALIDITY_CERT_NOT_AFTER, formatted);
}

TEST(UtilityTest, GetLastCryptoError) {
  // Clearing the error stack leaves us with no error to get.
  ERR_clear_error();
  EXPECT_FALSE(Utility::getLastCryptoError().has_value());

  ERR_put_error(ERR_LIB_SSL, 0, ERR_R_MALLOC_FAILURE, __FILE__, __LINE__);
  EXPECT_EQ(Utility::getLastCryptoError().value(),
            "error:10000041:SSL routines:OPENSSL_internal:malloc failure");

  // We consumed the last error, so back to not having an error to get.
  EXPECT_FALSE(Utility::getLastCryptoError().has_value());
}

TEST(UtilityTest, TestGetCertificationExtensionValue) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/extensions_cert.pem"));
  EXPECT_EQ("\xc\x9Something", Utility::getCertificateExtensionValue(*cert, "1.2.3.4.5.6.7.8"));
  EXPECT_EQ("\x30\x3\x1\x1\xFF", Utility::getCertificateExtensionValue(*cert, "1.2.3.4.5.6.7.9"));
  EXPECT_EQ("", Utility::getCertificateExtensionValue(*cert, "1.2.3.4.5.6.7.10"));
  EXPECT_EQ("", Utility::getCertificateExtensionValue(*cert, "1.2.3.4"));
  EXPECT_EQ("", Utility::getCertificateExtensionValue(*cert, ""));
  EXPECT_EQ("", Utility::getCertificateExtensionValue(*cert, "foo"));
}

TEST(UtilityTest, SslErrorDescriptionTest) {
  const std::vector<std::pair<int, std::string>> test_set = {
      {SSL_ERROR_NONE, "NONE"},
      {SSL_ERROR_SSL, "SSL"},
      {SSL_ERROR_WANT_READ, "WANT_READ"},
      {SSL_ERROR_WANT_WRITE, "WANT_WRITE"},
      {SSL_ERROR_WANT_PRIVATE_KEY_OPERATION, "WANT_PRIVATE_KEY_OPERATION"},
  };

  for (const auto& test_data : test_set) {
    EXPECT_EQ(test_data.second, Utility::getErrorDescription(test_data.first));
  }

  EXPECT_ENVOY_BUG(EXPECT_EQ(Utility::getErrorDescription(-1), "UNKNOWN_ERROR"),
                   "Unknown BoringSSL error had occurred");
}

TEST(UtilityTest, TestGetX509ErrorInfo) {
  auto cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  X509StoreContextPtr store_ctx = X509_STORE_CTX_new();
  X509StorePtr ssl_ctx = X509_STORE_new();
  EXPECT_TRUE(X509_STORE_CTX_init(store_ctx.get(), ssl_ctx.get(), cert.get(), nullptr));
  X509_STORE_CTX_set_error(store_ctx.get(), X509_V_ERR_UNSPECIFIED);
  EXPECT_EQ(Utility::getX509VerificationErrorInfo(store_ctx.get()),
            "X509_verify_cert: certificate verification error at depth 0: unknown certificate "
            "verification error");
}

} // namespace
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
