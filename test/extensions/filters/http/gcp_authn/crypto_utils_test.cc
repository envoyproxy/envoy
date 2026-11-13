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

TEST_F(CryptoUtilsTest, GetCertChainFromPemSuccess) {
  CertFingerprinterImpl fingerprinter;
  std::string cert_path =
      TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem");
  std::string cert_pem = TestEnvironment::readFileToStringForTest(cert_path);

  auto chain = fingerprinter.getCertChainFromPem(cert_pem);
  ASSERT_OK(chain);
  EXPECT_EQ(chain.value(), cert_pem);
}

TEST_F(CryptoUtilsTest, GetCertChainFromPemMultiCertChain) {
  CertFingerprinterImpl fingerprinter;
  std::string client_cert = TestEnvironment::readFileToStringForTest(
      TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"));
  std::string ca_cert = TestEnvironment::readFileToStringForTest(
      TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
  std::string full_chain = absl::StrCat(client_cert, ca_cert);

  auto chain = fingerprinter.getCertChainFromPem(full_chain);
  ASSERT_OK(chain);
  EXPECT_EQ(chain.value(), full_chain);
}

TEST_F(CryptoUtilsTest, GetCertChainFromPemStripsPrivateKeyAndExtraData) {
  CertFingerprinterImpl fingerprinter;
  std::string client_cert = TestEnvironment::readFileToStringForTest(
      TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"));
  std::string ca_cert = TestEnvironment::readFileToStringForTest(
      TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
  std::string client_key = TestEnvironment::readFileToStringForTest(
      TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));

  std::string bundle_pem =
      absl::StrCat("# Extra metadata comment\n", client_key, "\n", client_cert, ca_cert);

  auto chain = fingerprinter.getCertChainFromPem(bundle_pem);
  ASSERT_OK(chain);
  EXPECT_EQ(chain.value(), absl::StrCat(client_cert, ca_cert));
  EXPECT_THAT(chain.value(), testing::Not(testing::HasSubstr("PRIVATE KEY")));
}

TEST_F(CryptoUtilsTest, GetCertChainFromPemEmpty) {
  CertFingerprinterImpl fingerprinter;
  auto chain = fingerprinter.getCertChainFromPem("");
  EXPECT_THAT(chain, HasStatusMessage("Certificate PEM content is empty"));
}

TEST_F(CryptoUtilsTest, GetCertChainFromPemInvalid) {
  CertFingerprinterImpl fingerprinter;
  auto chain = fingerprinter.getCertChainFromPem("invalid cert content");
  EXPECT_THAT(chain, HasStatusMessage("Failed to parse certificate chain from PEM"));
}

TEST_F(CryptoUtilsTest, GetCertChainFromPemOnlyPrivateKeyFails) {
  CertFingerprinterImpl fingerprinter;
  std::string client_key = TestEnvironment::readFileToStringForTest(
      TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));

  auto chain = fingerprinter.getCertChainFromPem(client_key);
  EXPECT_THAT(chain, HasStatusMessage("Failed to parse certificate chain from PEM"));
}

TEST_F(CryptoUtilsTest, GetCertChainFromPemMalformedSecondCertFails) {
  CertFingerprinterImpl fingerprinter;
  std::string client_cert = TestEnvironment::readFileToStringForTest(
      TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"));
  std::string malformed_chain = absl::StrCat(
      client_cert, "-----BEGIN CERTIFICATE-----\ninvalid_base64!\n-----END CERTIFICATE-----\n");

  auto chain = fingerprinter.getCertChainFromPem(malformed_chain);
  EXPECT_THAT(chain, HasStatusMessage("Failed to parse certificate chain from PEM"));
}

} // namespace
} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
