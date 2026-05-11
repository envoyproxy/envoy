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

TEST_F(CryptoUtilsTest, GetBase64EncodedCertificateFingerprintSuccess) {
  auto secret_provider = std::make_shared<NiceMock<Secret::MockTlsCertificateConfigProvider>>();
  auto secret = std::make_unique<envoy::extensions::transport_sockets::tls::v3::TlsCertificate>();

  std::string cert_path =
      TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem");
  secret->mutable_certificate_chain()->set_filename(cert_path);

  EXPECT_CALL(*secret_provider, secret()).WillRepeatedly(Return(secret.get()));

  std::vector<Matchers::StringMatcherImpl> san_matchers;
  san_matchers.emplace_back(
      Matchers::StringMatcherImpl::createExactMatcher("spiffe://lyft.com/frontend-team"));

  auto fingerprint = getBase64EncodedCertificateFingerprint(secret_provider, san_matchers, *api_);
  ASSERT_TRUE(fingerprint.ok());
  // SHA256 fingerprint of
  // test/config/integration/certs/clientcert.pem
  // Base64 (unpadded): wH4U/EO5x7PZLxAE+R06ngcdnJOlivx2tMFDA646DzQ
  EXPECT_EQ(fingerprint.value(), "wH4U/EO5x7PZLxAE+R06ngcdnJOlivx2tMFDA646DzQ");
}

TEST_F(CryptoUtilsTest, GetBase64EncodedCertificateFingerprintNoSecret) {
  auto secret_provider = std::make_shared<NiceMock<Secret::MockTlsCertificateConfigProvider>>();
  EXPECT_CALL(*secret_provider, secret()).WillRepeatedly(Return(nullptr));

  std::vector<Matchers::StringMatcherImpl> san_matchers;
  auto fingerprint = getBase64EncodedCertificateFingerprint(secret_provider, san_matchers, *api_);
  EXPECT_FALSE(fingerprint.ok());
  EXPECT_EQ(fingerprint.status().message(), "Secret is null");
}

TEST_F(CryptoUtilsTest, GetBase64EncodedCertificateFingerprintSanMismatch) {
  auto secret_provider = std::make_shared<NiceMock<Secret::MockTlsCertificateConfigProvider>>();
  auto secret = std::make_unique<envoy::extensions::transport_sockets::tls::v3::TlsCertificate>();

  std::string cert_path =
      TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem");
  secret->mutable_certificate_chain()->set_filename(cert_path);

  EXPECT_CALL(*secret_provider, secret()).WillRepeatedly(Return(secret.get()));

  std::vector<Matchers::StringMatcherImpl> san_matchers;
  san_matchers.emplace_back(
      Matchers::StringMatcherImpl::createExactMatcher("spiffe://example.com/other-team"));

  auto fingerprint = getBase64EncodedCertificateFingerprint(secret_provider, san_matchers, *api_);
  EXPECT_FALSE(fingerprint.ok());
  EXPECT_EQ(fingerprint.status().message(), "Subject Alternative Names do not match");
}

TEST_F(CryptoUtilsTest, GetBase64EncodedCertificateFingerprintDefaultMatchersMismatch) {
  auto secret_provider = std::make_shared<NiceMock<Secret::MockTlsCertificateConfigProvider>>();
  auto secret = std::make_unique<envoy::extensions::transport_sockets::tls::v3::TlsCertificate>();

  std::string cert_path =
      TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem");
  secret->mutable_certificate_chain()->set_filename(cert_path);

  EXPECT_CALL(*secret_provider, secret()).WillRepeatedly(Return(secret.get()));

  std::vector<Matchers::StringMatcherImpl> san_matchers; // Empty
  auto fingerprint = getBase64EncodedCertificateFingerprint(secret_provider, san_matchers, *api_);
  EXPECT_FALSE(fingerprint.ok());
  EXPECT_EQ(fingerprint.status().message(), "Subject Alternative Names do not match");
}

TEST_F(CryptoUtilsTest, GetBase64EncodedCertificateFingerprintNullProvider) {
  std::vector<Matchers::StringMatcherImpl> san_matchers;
  auto fingerprint = getBase64EncodedCertificateFingerprint(nullptr, san_matchers, *api_);
  EXPECT_FALSE(fingerprint.ok());
  EXPECT_EQ(fingerprint.status().message(), "TLS certificate provider is null");
}

TEST_F(CryptoUtilsTest, GetBase64EncodedCertificateFingerprintDataSourceReadFailure) {
  auto secret_provider = std::make_shared<NiceMock<Secret::MockTlsCertificateConfigProvider>>();
  auto secret = std::make_unique<envoy::extensions::transport_sockets::tls::v3::TlsCertificate>();

  secret->mutable_certificate_chain()->set_filename("/nonexistent_file.pem");
  EXPECT_CALL(*secret_provider, secret()).WillRepeatedly(Return(secret.get()));

  std::vector<Matchers::StringMatcherImpl> san_matchers;
  auto fingerprint = getBase64EncodedCertificateFingerprint(secret_provider, san_matchers, *api_);
  EXPECT_FALSE(fingerprint.ok());
  EXPECT_EQ(fingerprint.status().message(), "Failed to read certificate from data source");
}

TEST_F(CryptoUtilsTest, GetBase64EncodedCertificateFingerprintEmptyContent) {
  auto secret_provider = std::make_shared<NiceMock<Secret::MockTlsCertificateConfigProvider>>();
  auto secret = std::make_unique<envoy::extensions::transport_sockets::tls::v3::TlsCertificate>();

  secret->mutable_certificate_chain()->set_inline_string("");
  EXPECT_CALL(*secret_provider, secret()).WillRepeatedly(Return(secret.get()));

  std::vector<Matchers::StringMatcherImpl> san_matchers;
  auto fingerprint = getBase64EncodedCertificateFingerprint(secret_provider, san_matchers, *api_);
  EXPECT_FALSE(fingerprint.ok());
  EXPECT_EQ(fingerprint.status().message(), "Certificate file content is empty");
}

TEST_F(CryptoUtilsTest, GetBase64EncodedCertificateFingerprintParseFailure) {
  auto secret_provider = std::make_shared<NiceMock<Secret::MockTlsCertificateConfigProvider>>();
  auto secret = std::make_unique<envoy::extensions::transport_sockets::tls::v3::TlsCertificate>();

  secret->mutable_certificate_chain()->set_inline_string("invalid cert content");
  EXPECT_CALL(*secret_provider, secret()).WillRepeatedly(Return(secret.get()));

  std::vector<Matchers::StringMatcherImpl> san_matchers;
  auto fingerprint = getBase64EncodedCertificateFingerprint(secret_provider, san_matchers, *api_);
  EXPECT_FALSE(fingerprint.ok());
  EXPECT_EQ(fingerprint.status().message(), "Failed to parse certificate");
}

} // namespace
} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
