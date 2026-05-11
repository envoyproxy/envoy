#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/common/callback.h"
#include "envoy/extensions/transport_sockets/tls/v3/common.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/secret.pb.h"
#include "envoy/init/target.h"
#include "envoy/secret/secret_provider.h"

#include "source/common/common/matchers.h"
#include "source/extensions/filters/http/gcp_authn/crypto_utils.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Secret {

class MockTlsCertificateConfigProvider : public TlsCertificateConfigProvider {
public:
  MockTlsCertificateConfigProvider();
  ~MockTlsCertificateConfigProvider() override;

  MOCK_METHOD(const envoy::extensions::transport_sockets::tls::v3::TlsCertificate*, secret, (),
              (const));
  MOCK_METHOD(Common::CallbackHandlePtr, addValidationCallback,
              (std::function<absl::Status(
                   const envoy::extensions::transport_sockets::tls::v3::TlsCertificate&)>));
  MOCK_METHOD(Common::CallbackHandlePtr, addUpdateCallback, (std::function<absl::Status()>));
  MOCK_METHOD(Common::CallbackHandlePtr, addRemoveCallback, (std::function<absl::Status()>));
  MOCK_METHOD(const Init::Target*, initTarget, ());
  MOCK_METHOD(void, start, ());
};

MockTlsCertificateConfigProvider::MockTlsCertificateConfigProvider() = default;
MockTlsCertificateConfigProvider::~MockTlsCertificateConfigProvider() = default;

} // namespace Secret

namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {
namespace {

using testing::NiceMock;
using testing::Return;

class CryptoUtilsTest : public testing::Test {
public:
  CryptoUtilsTest() : api_(Api::createApiForTest()) {}

  Api::ApiPtr api_;
};

TEST_F(CryptoUtilsTest, GetCertificateFingerprintSuccess) {
  auto secret_provider = std::make_shared<NiceMock<Secret::MockTlsCertificateConfigProvider>>();
  auto secret = std::make_unique<envoy::extensions::transport_sockets::tls::v3::TlsCertificate>();

  std::string cert_path =
      TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem");
  secret->mutable_certificate_chain()->set_filename(cert_path);

  EXPECT_CALL(*secret_provider, secret()).WillRepeatedly(Return(secret.get()));

  std::vector<Matchers::StringMatcherImpl> san_matchers;
  san_matchers.emplace_back(
      Matchers::StringMatcherImpl::createExactMatcher("spiffe://lyft.com/frontend-team"));

  auto fingerprint = getCertificateFingerprint(secret_provider, san_matchers, *api_);
  ASSERT_TRUE(fingerprint.has_value());
  // SHA256 fingerprint of
  // test/config/integration/certs/clientcert.pem
  // Base64 (unpadded): wH4U/EO5x7PZLxAE%252BR06ngcdnJOlivx2tMFDA646DzQ
  // This verifies that '/' is not encoded.
  EXPECT_EQ(fingerprint.value(), "wH4U/EO5x7PZLxAE%252BR06ngcdnJOlivx2tMFDA646DzQ");
}

TEST_F(CryptoUtilsTest, GetCertificateFingerprintNoSecret) {
  auto secret_provider = std::make_shared<NiceMock<Secret::MockTlsCertificateConfigProvider>>();
  EXPECT_CALL(*secret_provider, secret()).WillRepeatedly(Return(nullptr));

  std::vector<Matchers::StringMatcherImpl> san_matchers;
  auto fingerprint = getCertificateFingerprint(secret_provider, san_matchers, *api_);
  EXPECT_FALSE(fingerprint.has_value());
}

TEST_F(CryptoUtilsTest, GetCertificateFingerprintSanMismatch) {
  auto secret_provider = std::make_shared<NiceMock<Secret::MockTlsCertificateConfigProvider>>();
  auto secret = std::make_unique<envoy::extensions::transport_sockets::tls::v3::TlsCertificate>();

  std::string cert_path =
      TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem");
  secret->mutable_certificate_chain()->set_filename(cert_path);

  EXPECT_CALL(*secret_provider, secret()).WillRepeatedly(Return(secret.get()));

  std::vector<Matchers::StringMatcherImpl> san_matchers;
  san_matchers.emplace_back(
      Matchers::StringMatcherImpl::createExactMatcher("spiffe://example.com/other-team"));

  auto fingerprint = getCertificateFingerprint(secret_provider, san_matchers, *api_);
  EXPECT_FALSE(fingerprint.has_value());
}

TEST_F(CryptoUtilsTest, GetCertificateFingerprintDefaultMatchersMismatch) {
  auto secret_provider = std::make_shared<NiceMock<Secret::MockTlsCertificateConfigProvider>>();
  auto secret = std::make_unique<envoy::extensions::transport_sockets::tls::v3::TlsCertificate>();

  std::string cert_path =
      TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem");
  secret->mutable_certificate_chain()->set_filename(cert_path);

  EXPECT_CALL(*secret_provider, secret()).WillRepeatedly(Return(secret.get()));

  std::vector<Matchers::StringMatcherImpl> san_matchers; // Empty
  auto fingerprint = getCertificateFingerprint(secret_provider, san_matchers, *api_);
  EXPECT_FALSE(fingerprint.has_value());
}

TEST_F(CryptoUtilsTest, GetCertificateFingerprintWithPlus) {
  auto secret_provider = std::make_shared<NiceMock<Secret::MockTlsCertificateConfigProvider>>();
  auto secret = std::make_unique<envoy::extensions::transport_sockets::tls::v3::TlsCertificate>();

  constexpr char cert_pem[] = R"(-----BEGIN CERTIFICATE-----
MIIC9zCCAd+gAwIBAgIUQT6jz9u0zSJG1JSszwQjcMOmVJswDQYJKoZIhvcNAQEL
BQAwHjEcMBoGA1UEAwwTVGVzdCBDZXJ0IFdpdGggUGx1czAeFw0yNjAyMjQyMTQx
MTNaFw0yNjAzMDYyMTQxMTNaMB4xHDAaBgNVBAMME1Rlc3QgQ2VydCBXaXRoIFBs
dXMwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQC9LO/ZTqFmItOG2pDL
KXmvtPZdxsSbEY4B/E2J6xiSl3e7w+pqhTb9kNWw+0PAR5pwEV1CDJsniQR99axP
tv4i1H5ySA/+elrSdj5JM9TLo4lbc78sMzWWolhDFGoFs90CYFniUArzDBUTLAi1
c58qJnLrk6GEC6nbDltuonqGv3lbFlOp8glPUQrlvsX4jXfN+qB7J90aZs/kejeo
ZNwd/dr+pLiBDlZLbCpYvjBEAOZiK5blmNLVrnGnVWXr+GiHPtQ8hy1wRshXjGcr
Mixwrb2N1ODAuNjinBjeLnUKeOa+IbQI2D1F13q955/feNtxK79ks9Zlajn+6a9O
DP1TAgMBAAGjLTArMCkGA1UdEQQiMCCGHnNwaWZmZTovL2V4YW1wbGUuY29tL3Bs
dXMtdGVhbTANBgkqhkiG9w0BAQsFAAOCAQEAEYGmGE19CtxU/wKLpSv5HcKfkcLV
XyXjyF4orhueTBMS02Z91sWJberiFD7aahrEnvekgMzF+egn40lnQLzlMleVZhrG
ii00s79A+ktdrOUqk2HEPmgJTRB4B0uf7n9jsgJ6x4bLtxBlF4Ls7oMut/v/xY++
6kXuwN27aDNW/8aRwKO6qKfV+VjEhMbfyZ7986pZGba3r+i1IQmPR46aVfhsFXL2
bIxIOGooYAHpRRmAZUYf6bNCluFdbN5alXBCBQWOkB9FzmFYeIuZ6ovOVfnk4SK3
rVK2CPS4inZGFccHk/L7soDu6kXZDQw8YqsBigLhLXtEBQkeNASaatg2Ig==
-----END CERTIFICATE-----)";
  secret->mutable_certificate_chain()->set_inline_string(cert_pem);

  EXPECT_CALL(*secret_provider, secret()).WillRepeatedly(Return(secret.get()));

  std::vector<Matchers::StringMatcherImpl> san_matchers;
  san_matchers.emplace_back(
      Matchers::StringMatcherImpl::createExactMatcher("spiffe://example.com/plus-team"));

  auto fingerprint = getCertificateFingerprint(secret_provider, san_matchers, *api_);
  ASSERT_TRUE(fingerprint.has_value());
  // Original Base64: nc+YEzVsw5JTJFPIfFphOHVDsy8uE1yGpQRI2s8w6LU
  // Expected (with double replacement):
  // nc%252BYEzVsw5JTJFPIfFphOHVDsy8uE1yGpQRI2s8w6LU
  EXPECT_EQ(fingerprint.value(), "nc%252BYEzVsw5JTJFPIfFphOHVDsy8uE1yGpQRI2s8w6LU");
}

} // namespace
} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
