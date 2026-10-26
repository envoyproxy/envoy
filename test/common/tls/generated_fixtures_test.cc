#include <cstdint>
#include <string>
#include <vector>

#include "source/common/tls/ocsp/ocsp.h"

#include "test/common/tls/ssl_test_utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"
#include "openssl/x509.h"

// Verifies that the fixtures @envoy_toolshed//certs:gen produces match the
// generator spec (validity windows, OCSP response contents, etc). This is not
// a test of OCSP/certificate parsing logic in general -- see ocsp_test.cc and
// utility_test.cc for that -- it only exercises the properties that are
// specific to the generated fixtures, such as the `next_update_days = 730`
// guarantee. This replaces the human-readable `*_ocsp_resp_details.txt` dumps
// that the old offline fixture generation scripts wrote next to each
// response.
namespace Envoy {
namespace {

using Extensions::TransportSockets::Tls::Ocsp::OcspResponseWrapperImpl;

class GeneratedOcspResponseTest : public testing::Test {
public:
  std::vector<uint8_t> readResponse(const std::string& filename) {
    const auto str = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        "{{ test_rundir }}/test/common/tls/ocsp/test_data/" + filename));
    return {str.begin(), str.end()};
  }

protected:
  Event::SimulatedTimeSystem time_system_;
};

// The generator stamps `next_update_days = 730` onto this fixture, so it must
// stay valid for the best part of two years after every regeneration.
TEST_F(GeneratedOcspResponseTest, GoodResponseIsValid) {
  auto response = OcspResponseWrapperImpl::create(readResponse("good_ocsp_resp.der"), time_system_);
  ASSERT_TRUE(response.ok());

  EXPECT_FALSE((*response)->isExpired());
  EXPECT_GT((*response)->secondsUntilExpiration(), 0);
}

// Responses generated without a nextUpdate are always treated as expired.
TEST_F(GeneratedOcspResponseTest, ResponseWithoutNextUpdateIsExpired) {
  auto response =
      OcspResponseWrapperImpl::create(readResponse("revoked_ocsp_resp.der"), time_system_);
  ASSERT_TRUE(response.ok());

  EXPECT_TRUE((*response)->isExpired());
  EXPECT_EQ(0, (*response)->secondsUntilExpiration());
}

// The validity window of the generated certificates depends on the year the
// build was stamped with, so check that fixtures which are meant to be current
// really are, and that the expired one really is expired.
//
// This intentionally uses the real system clock via `X509_cmp_current_time`,
// contrary to the usual hermetic-time rule, because the point of the test is
// to verify that the build-stamped fixtures are valid *right now*. Do not
// convert this to `SimulatedTimeSystem`.
class GeneratedCertValidityTest : public testing::Test {
public:
  bssl::UniquePtr<X509> readCert(const std::string& path) {
    return Extensions::TransportSockets::Tls::readCertFromFile(
        TestEnvironment::substitute("{{ test_rundir }}/" + path));
  }

  void expectCurrentlyValid(const std::string& path) {
    auto cert = readCert(path);
    ASSERT_NE(nullptr, cert.get()) << path;
    // `X509_cmp_current_time` returns <0 when the time is in the past.
    EXPECT_LT(X509_cmp_current_time(X509_get0_notBefore(cert.get())), 0) << path;
    EXPECT_GT(X509_cmp_current_time(X509_get0_notAfter(cert.get())), 0) << path;
  }
};

TEST_F(GeneratedCertValidityTest, ExpiredFixtureIsExpired) {
  auto cert = readCert("test/config/integration/certs/expired_cert.pem");
  ASSERT_NE(nullptr, cert.get()) << "expired_cert.pem";
  EXPECT_LT(X509_cmp_current_time(X509_get0_notAfter(cert.get())), 0);
}

TEST_F(GeneratedCertValidityTest, CurrentFixturesAreValid) {
  expectCurrentlyValid("test/config/integration/certs/servercert.pem");
  expectCurrentlyValid("test/common/tls/test_data/san_dns_cert.pem");
}

} // namespace
} // namespace Envoy
