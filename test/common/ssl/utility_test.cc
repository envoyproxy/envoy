#include <string>
#include <vector>

#include "common/ssl/utility.h"

#include "test/common/ssl/ssl_test_utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Ssl {

TEST(UtilityTest, TestGetSubjectAlternateNamesWithDNS) {
  bssl::UniquePtr<X509> cert = readCertFromFile("test/common/ssl/test_data/san_dns_cert.pem");
  const std::vector<std::string>& subject_alt_names = Utility::getSubjectAltNames(*cert, GEN_DNS);
  EXPECT_EQ(1, subject_alt_names.size());
}

TEST(UtilityTest, TestMultipleGetSubjectAlternateNamesWithDNS) {
  bssl::UniquePtr<X509> cert =
      readCertFromFile("test/common/ssl/test_data/san_multiple_dns_cert.pem");
  const std::vector<std::string>& subject_alt_names = Utility::getSubjectAltNames(*cert, GEN_DNS);
  EXPECT_EQ(2, subject_alt_names.size());
}

TEST(UtilityTest, TestGetSubjectAlternateNamesWithUri) {
  bssl::UniquePtr<X509> cert = readCertFromFile("test/common/ssl/test_data/san_uri_cert.pem");
  const std::vector<std::string>& subject_alt_names = Utility::getSubjectAltNames(*cert, GEN_URI);
  EXPECT_EQ(1, subject_alt_names.size());
}

TEST(UtilityTest, TestGetSubjectAlternateNamesWithNoSAN) {
  bssl::UniquePtr<X509> cert = readCertFromFile("test/common/ssl/test_data/no_san_cert.pem");
  const std::vector<std::string>& uri_subject_alt_names =
      Utility::getSubjectAltNames(*cert, GEN_URI);
  EXPECT_EQ(0, uri_subject_alt_names.size());
}

TEST(UtilityTest, TestGetSubject) {
  bssl::UniquePtr<X509> cert = readCertFromFile("test/common/ssl/test_data/san_dns_cert.pem");
  EXPECT_EQ("CN=Test Server,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US",
            Utility::getSubjectFromCertificate(*cert));
}

TEST(UtilityTest, TestGetSerialNumber) {
  bssl::UniquePtr<X509> cert = readCertFromFile("test/common/ssl/test_data/san_dns_cert.pem");
  EXPECT_EQ("f3828eb24fd779d0", Utility::getSerialNumberFromCertificate(*cert));
}

TEST(UtilityTest, TestDaysUntilExpiration) {
  bssl::UniquePtr<X509> cert = readCertFromFile("test/common/ssl/test_data/san_dns_cert.pem");
  EXPECT_EQ(270, Utility::getDaysUntilExpiration(cert.get()));
}

TEST(UtilityTest, TestDaysUntilExpirationWithNull) {
  EXPECT_EQ(std::numeric_limits<int>::max(), Utility::getDaysUntilExpiration(nullptr));
}

} // namespace Ssl
} // namespace Envoy