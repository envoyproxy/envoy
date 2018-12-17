#include <string>
#include <vector>

#include "common/ssl/utility.h"

#include "test/common/ssl/ssl_test_utility.h"
#include "test/common/ssl/test_data/san_dns_cert_info.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Ssl {

TEST(UtilityTest, TestGetSubjectAlternateNamesWithDNS) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  const std::vector<std::string>& subject_alt_names = Utility::getSubjectAltNames(*cert, GEN_DNS);
  EXPECT_EQ(1, subject_alt_names.size());
}

TEST(UtilityTest, TestMultipleGetSubjectAlternateNamesWithDNS) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/ssl/test_data/san_multiple_dns_cert.pem"));
  const std::vector<std::string>& subject_alt_names = Utility::getSubjectAltNames(*cert, GEN_DNS);
  EXPECT_EQ(2, subject_alt_names.size());
}

TEST(UtilityTest, TestGetSubjectAlternateNamesWithUri) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_uri_cert.pem"));
  const std::vector<std::string>& subject_alt_names = Utility::getSubjectAltNames(*cert, GEN_URI);
  EXPECT_EQ(1, subject_alt_names.size());
}

TEST(UtilityTest, TestGetSubjectAlternateNamesWithNoSAN) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem"));
  const std::vector<std::string>& uri_subject_alt_names =
      Utility::getSubjectAltNames(*cert, GEN_URI);
  EXPECT_EQ(0, uri_subject_alt_names.size());
}

TEST(UtilityTest, TestGetSubject) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  EXPECT_EQ("CN=Test Server,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US",
            Utility::getSubjectFromCertificate(*cert));
}

TEST(UtilityTest, TestGetSerialNumber) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  EXPECT_EQ(TEST_SAN_DNS_CERT_SERIAL, Utility::getSerialNumberFromCertificate(*cert));
}

TEST(UtilityTest, TestDaysUntilExpiration) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  // Set a known date (2033-05-18 03:33:20 UTC) so that we get fixed output from this test.
  const time_t known_date_time = 2000000000;
  Event::SimulatedTimeSystem time_source;
  time_source.setSystemTime(std::chrono::system_clock::from_time_t(known_date_time));

  // Get expiration time from the certificate info.
  std::tm expiration{};
  std::istringstream text(TEST_SAN_DNS_CERT_NOT_AFTER);
  text >> std::get_time(&expiration, "%b %e %H:%M:%S %Y GMT");
  EXPECT_FALSE(text.fail());

  int days = std::difftime(std::mktime(&expiration), known_date_time) / (60 * 60 * 24);
  EXPECT_EQ(days, Utility::getDaysUntilExpiration(cert.get(), time_source));
}

TEST(UtilityTest, TestDaysUntilExpirationWithNull) {
  Event::SimulatedTimeSystem time_source;
  EXPECT_EQ(std::numeric_limits<int>::max(), Utility::getDaysUntilExpiration(nullptr, time_source));
}

TEST(UtilityTest, TestValidFrom) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  const time_t valid_from = std::chrono::system_clock::to_time_t(Utility::getValidFrom(*cert));
  char buffer[25];
  size_t len = strftime(buffer, sizeof(buffer), "%b %e %H:%M:%S %Y GMT", localtime(&valid_from));
  ASSERT(len == sizeof(buffer) - 1);
  EXPECT_EQ(TEST_SAN_DNS_CERT_NOT_BEFORE, std::string(buffer));
}

TEST(UtilityTest, TestExpirationTime) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  const time_t expiration = std::chrono::system_clock::to_time_t(Utility::getExpirationTime(*cert));
  char buffer[25];
  size_t len = strftime(buffer, sizeof(buffer), "%b %e %H:%M:%S %Y GMT", localtime(&expiration));
  ASSERT(len == sizeof(buffer) - 1);
  EXPECT_EQ(TEST_SAN_DNS_CERT_NOT_AFTER, std::string(buffer));
}
} // namespace Ssl
} // namespace Envoy
