#include <string>

#include "envoy/api/api.h"

#include "source/extensions/filters/http/gcp_authn/crypto_utils.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {
namespace {

class CryptoUtilsTest : public testing::Test {
public:
  CryptoUtilsTest() : api_(Api::createApiForTest()) {}

  Api::ApiPtr api_;
};

TEST_F(CryptoUtilsTest, GetFingerprintFromPemSuccess) {
  CertFingerprinterImpl fingerprinter;
  std::string cert_path =
      TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem");
  std::string cert_pem = TestEnvironment::readFileToStringForTest(cert_path);

  auto fingerprint = fingerprinter.getFingerprintFromPem(cert_pem);
  ASSERT_TRUE(fingerprint.ok());
  // SPELLCHECKER(off)
  EXPECT_EQ(fingerprint.value(), "wH4U/EO5x7PZLxAE+R06ngcdnJOlivx2tMFDA646DzQ");
  // SPELLCHECKER(on)
}

TEST_F(CryptoUtilsTest, GetFingerprintFromPemEmpty) {
  CertFingerprinterImpl fingerprinter;
  auto fingerprint = fingerprinter.getFingerprintFromPem("");
  EXPECT_FALSE(fingerprint.ok());
  EXPECT_EQ(fingerprint.status().message(), "Certificate PEM content is empty");
}

TEST_F(CryptoUtilsTest, GetFingerprintFromPemInvalid) {
  CertFingerprinterImpl fingerprinter;
  auto fingerprint = fingerprinter.getFingerprintFromPem("invalid cert content");
  EXPECT_FALSE(fingerprint.ok());
  EXPECT_EQ(fingerprint.status().message(), "Failed to parse certificate from PEM");
}

} // namespace
} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
