#include <string>

#include "envoy/api/api.h"

#include "source/common/common/base64.h"
#include "source/common/common/hex.h"
#include "source/extensions/filters/http/gcp_authn/crypto_utils.h"

#include "test/config/integration/certs/clientcert_hash.h"
#include "test/test_common/environment.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "absl/strings/str_replace.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {
namespace {

using ::Envoy::StatusHelpers::HasStatusMessage;

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
  ASSERT_OK(fingerprint);
  const std::vector<uint8_t> raw =
      Hex::decode(absl::StrReplaceAll(TEST_CLIENT_CERT_HASH, {{":", ""}}));
  EXPECT_EQ(fingerprint.value(),
            Base64::encode(reinterpret_cast<const char*>(raw.data()), raw.size(), false));
}

TEST_F(CryptoUtilsTest, GetFingerprintFromPemEmpty) {
  CertFingerprinterImpl fingerprinter;
  auto fingerprint = fingerprinter.getFingerprintFromPem("");
  EXPECT_THAT(fingerprint, HasStatusMessage("Certificate PEM content is empty"));
}

TEST_F(CryptoUtilsTest, GetFingerprintFromPemInvalid) {
  CertFingerprinterImpl fingerprinter;
  auto fingerprint = fingerprinter.getFingerprintFromPem("invalid cert content");
  EXPECT_THAT(fingerprint, HasStatusMessage("Failed to parse certificate from PEM"));
}

} // namespace
} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
