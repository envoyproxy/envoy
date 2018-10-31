#include <string>
#include <vector>

#include "common/ssl/utility.h"

#include "test/common/ssl/ssl_test_utility.h"
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
  EXPECT_EQ("f3828eb24fd779d0", Utility::getSerialNumberFromCertificate(*cert));
}

TEST(UtilityTest, TestDaysUntilExpiration) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem"));
  // Set a known date (10/21/2018 00:00:00) so that we get fixed output from this test.
  const long known_date_time = 1540080000;
  Event::SimulatedTimeSystem time_source;
  time_source.setSystemTime(std::chrono::system_clock::from_time_t(known_date_time));
  EXPECT_EQ(261, Utility::getDaysUntilExpiration(cert.get(), time_source));
}

TEST(UtilityTest, TestDaysUntilExpirationWithNull) {
  Event::SimulatedTimeSystem time_source;
  EXPECT_EQ(std::numeric_limits<int>::max(), Utility::getDaysUntilExpiration(nullptr, time_source));
}

TEST(UtilityTest, TestValidFrom) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert3.pem"));
  const time_t valid_from = std::chrono::system_clock::to_time_t(Utility::getValidFrom(*cert));
  EXPECT_EQ("Mon Jan 15 22:40:27 2018\n", std::string(ctime(&valid_from)));
}

TEST(UtilityTest, TestExpirationTimeWithExpiredCert) {
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert3.pem"));
  const time_t expiration_time =
      std::chrono::system_clock::to_time_t(Utility::getExpirationTime(*cert));
  EXPECT_EQ("Wed Jan 15 22:40:27 2020\n", std::string(ctime(&expiration_time)));
}
} // namespace Ssl
} // namespace Envoy
