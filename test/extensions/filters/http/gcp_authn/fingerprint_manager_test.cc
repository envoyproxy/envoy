#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/extensions/filters/http/gcp_authn/fingerprint_manager.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "envoy/common/callback.h"
#include "test/mocks/secret/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Secret {

class MockTlsCertificateConfigProvider : public TlsCertificateConfigProvider {
public:
  MockTlsCertificateConfigProvider() = default;
  ~MockTlsCertificateConfigProvider() override = default;

  MOCK_METHOD(const envoy::extensions::transport_sockets::tls::v3::TlsCertificate*, secret, (), (const));
  MOCK_METHOD(Common::CallbackHandlePtr, addValidationCallback,
              (std::function<absl::Status(
                   const envoy::extensions::transport_sockets::tls::v3::TlsCertificate&)>));
  MOCK_METHOD(Common::CallbackHandlePtr, addUpdateCallback, (std::function<absl::Status()>));
  MOCK_METHOD(Common::CallbackHandlePtr, addRemoveCallback, (std::function<absl::Status()>));
  MOCK_METHOD(const Init::Target*, initTarget, ());
  MOCK_METHOD(void, start, ());
};

} // namespace Secret

namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {
namespace {

using ::envoy::extensions::filters::http::gcp_authn::v3::TokenBindingConfig;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::_;

class DummyCallbackHandle : public ::Envoy::Common::CallbackHandle {
public:
  ~DummyCallbackHandle() override = default;
};

class FingerprintManagerTest : public testing::Test {
protected:
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

TEST_F(FingerprintManagerTest, CertNotFound) {
  std::string config_yaml = R"EOF(
    client_certificate:
      name: missing_cert_secret
    client_certificate_san_matchers:
      - exact: "test.com"
  )EOF";
  TokenBindingConfig config;
  TestUtility::loadFromYaml(config_yaml, config);

  FingerprintManager manager(config, context_);
  EXPECT_EQ(manager.fingerprint(), absl::nullopt);
}

TEST_F(FingerprintManagerTest, ValidCertAndSanMatch) {
  const std::string cert_pem = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/non_spiffe_san_cert.pem"));

  envoy::extensions::transport_sockets::tls::v3::Secret secret;
  secret.set_name("client_cert_secret");
  auto* tls_cert = secret.mutable_tls_certificate();
  tls_cert->mutable_certificate_chain()->set_inline_string(cert_pem);

  auto status = context_.server_factory_context_.secretManager().addStaticSecret(secret);
  EXPECT_TRUE(status.ok());

  std::string config_yaml = R"EOF(
    client_certificate:
      name: client_cert_secret
    client_certificate_san_matchers:
      - exact: "test.com"
  )EOF";
  TokenBindingConfig config;
  TestUtility::loadFromYaml(config_yaml, config);

  FingerprintManager manager(config, context_);
  EXPECT_NE(manager.fingerprint(), absl::nullopt);
  EXPECT_FALSE(manager.fingerprint().value().empty());
}

TEST_F(FingerprintManagerTest, SanMismatch) {
  const std::string cert_pem = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/non_spiffe_san_cert.pem"));

  envoy::extensions::transport_sockets::tls::v3::Secret secret;
  secret.set_name("client_cert_secret");
  auto* tls_cert = secret.mutable_tls_certificate();
  tls_cert->mutable_certificate_chain()->set_inline_string(cert_pem);

  auto status = context_.server_factory_context_.secretManager().addStaticSecret(secret);
  EXPECT_TRUE(status.ok());

  std::string config_yaml = R"EOF(
    client_certificate:
      name: client_cert_secret
    client_certificate_san_matchers:
      - exact: "mismatch.com"
  )EOF";
  TokenBindingConfig config;
  TestUtility::loadFromYaml(config_yaml, config);

  FingerprintManager manager(config, context_);
  EXPECT_EQ(manager.fingerprint(), absl::nullopt);
}

TEST_F(FingerprintManagerTest, DynamicSdsSecretUpdates) {
  auto* mock_secret_manager = new NiceMock<Secret::MockSecretManager>();
  context_.server_factory_context_.secret_manager_.reset(mock_secret_manager);

  auto mock_provider = std::make_shared<NiceMock<Secret::MockTlsCertificateConfigProvider>>();
  EXPECT_CALL(*mock_secret_manager, findOrCreateTlsCertificateProvider(_, "client_cert_secret", _, _, _))
      .WillOnce(Return(mock_provider));

  std::function<absl::Status()> update_callback;
  EXPECT_CALL(*mock_provider, addUpdateCallback(_))
      .WillOnce(Invoke([&update_callback](std::function<absl::Status()> cb) {
        update_callback = cb;
        return std::make_unique<DummyCallbackHandle>();
      }));

  std::string config_yaml = R"EOF(
    client_certificate:
      name: client_cert_secret
      sds_config:
        api_config_source:
          api_type: GRPC
          grpc_services:
            - envoy_grpc:
                cluster_name: sds_cluster
    client_certificate_san_matchers:
      - exact: "test.com"
  )EOF";
  TokenBindingConfig config;
  TestUtility::loadFromYaml(config_yaml, config);

  // 1. Constructing with no secret yet
  EXPECT_CALL(*mock_provider, secret()).WillRepeatedly(Return(nullptr));
  FingerprintManager manager(config, context_);
  EXPECT_EQ(manager.fingerprint(), absl::nullopt);

  // 2. Trigger update with a valid certificate
  const std::string cert_pem = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/non_spiffe_san_cert.pem"));
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate tls_cert;
  tls_cert.mutable_certificate_chain()->set_inline_string(cert_pem);

  EXPECT_CALL(*mock_provider, secret()).WillRepeatedly(Return(&tls_cert));
  auto status = update_callback();
  EXPECT_TRUE(status.ok());

  // Fingerprint should be populated
  EXPECT_TRUE(manager.fingerprint().has_value());

  // 3. Trigger update with an invalid certificate (empty content) -> fingerprint gets cleared
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate invalid_cert;
  invalid_cert.mutable_certificate_chain()->set_inline_string("");

  EXPECT_CALL(*mock_provider, secret()).WillRepeatedly(Return(&invalid_cert));
  status = update_callback();
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(manager.fingerprint(), absl::nullopt);
}

} // namespace
} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
