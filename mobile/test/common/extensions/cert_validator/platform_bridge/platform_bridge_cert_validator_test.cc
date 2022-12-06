#include <memory>
#include <string>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/crypto/crypto_impl.h"
#include "source/common/crypto/utility.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/extensions/transport_sockets/tls/cert_validator/default_validator.h"
#include "source/extensions/transport_sockets/tls/cert_validator/san_matcher.h"
#include "source/extensions/transport_sockets/tls/stats.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/extensions/transport_sockets/tls/cert_validator/test_common.h"
#include "test/extensions/transport_sockets/tls/ssl_test_utility.h"
#include "test/extensions/transport_sockets/tls/test_data/san_dns2_cert_info.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "library/common/data/utility.h"
#include "library/common/extensions/cert_validator/platform_bridge/config.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

using SSLContextPtr = Envoy::CSmartPtr<SSL_CTX, SSL_CTX_free>;

using envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext;

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrEq;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class MockValidateResultCallback : public Ssl::ValidateResultCallback {
public:
  ~MockValidateResultCallback() override = default;

  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(void, onCertValidationResult, (bool, const std::string&, uint8_t));
};

class MockValidator {
public:
  MOCK_METHOD(void, cleanup, ());
  MOCK_METHOD(envoy_cert_validation_result, validate, (const envoy_data*, uint8_t, const char*));
};

class PlatformBridgeCertValidatorTest
    : public testing::TestWithParam<CertificateValidationContext::TrustChainVerification> {
protected:
  PlatformBridgeCertValidatorTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")),
        stats_(generateSslStats(test_store_)), ssl_ctx_(SSL_CTX_new(TLS_method())),
        callback_(std::make_unique<MockValidateResultCallback>()), is_server_(false) {
    mock_validator_ = std::make_unique<MockValidator>();
    main_thread_id_ = std::this_thread::get_id();

    platform_validator_.validate_cert = (PlatformBridgeCertValidatorTest::validate);
    platform_validator_.validation_cleanup = (PlatformBridgeCertValidatorTest::cleanup);
  }

  void initializeConfig() {
    EXPECT_CALL(config_, caCert()).WillOnce(ReturnRef(empty_string_));
    EXPECT_CALL(config_, certificateRevocationList()).WillOnce(ReturnRef(empty_string_));
    EXPECT_CALL(config_, trustChainVerification()).WillOnce(Return(GetParam()));
  }

  ~PlatformBridgeCertValidatorTest() {
    mock_validator_.reset();
    main_thread_id_ = std::thread::id();
    Envoy::Assert::resetEnvoyBugCountersForTest();
  }

  ABSL_MUST_USE_RESULT bool waitForDispatcherToExit() {
    Event::TimerPtr timer(dispatcher_->createTimer([this]() -> void { dispatcher_->exit(); }));
    timer->enableTimer(std::chrono::milliseconds(100));
    dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
    return !timer->enabled();
  }

  bool acceptInvalidCertificates() {
    return GetParam() == CertificateValidationContext::ACCEPT_UNTRUSTED;
  }

  static envoy_cert_validation_result validate(const envoy_data* certs, uint8_t size,
                                               const char* hostname) {
    // Validate must be called on the worker thread, not the main thread.
    EXPECT_NE(main_thread_id_, std::this_thread::get_id());

    // Make sure the cert was converted correctly.
    const Buffer::InstancePtr buffer = Data::Utility::toInternalData(*certs);
    const auto digest = Common::Crypto::UtilitySingleton::get().getSha256Digest(*buffer);
    EXPECT_EQ(TEST_SAN_DNS2_CERT_256_HASH, Hex::encode(digest));
    return mock_validator_->validate(certs, size, hostname);
  }

  static void cleanup() {
    // Validate must be called on the worker thread, not the main thread.
    EXPECT_NE(main_thread_id_, std::this_thread::get_id());
    mock_validator_->cleanup();
  }

  static std::unique_ptr<MockValidator> mock_validator_;
  static std::thread::id main_thread_id_;

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Stats::TestUtil::TestStore test_store_;
  SslStats stats_;
  Ssl::MockCertificateValidationContextConfig config_;
  std::string empty_string_;
  SSLContextPtr ssl_ctx_;
  TestSslExtendedSocketInfo ssl_extended_info_;
  CertValidator::ExtraValidationContext validation_context_;
  Network::TransportSocketOptionsConstSharedPtr transport_socket_options_;
  std::unique_ptr<MockValidateResultCallback> callback_;
  bool is_server_;
  envoy_cert_validator platform_validator_;
};

std::unique_ptr<MockValidator> PlatformBridgeCertValidatorTest::mock_validator_;
std::thread::id PlatformBridgeCertValidatorTest::main_thread_id_;

INSTANTIATE_TEST_SUITE_P(TrustMode, PlatformBridgeCertValidatorTest,
                         testing::ValuesIn({CertificateValidationContext::VERIFY_TRUST_CHAIN,
                                            CertificateValidationContext::ACCEPT_UNTRUSTED}));

TEST_P(PlatformBridgeCertValidatorTest, NoConfig) {
  EXPECT_ENVOY_BUG(
      { PlatformBridgeCertValidator validator(nullptr, stats_, &platform_validator_); },
      "Invalid certificate validation context config.");
}

TEST_P(PlatformBridgeCertValidatorTest, NonEmptyCaCert) {
  std::string ca_cert = "xyz";
  EXPECT_CALL(config_, caCert()).WillRepeatedly(ReturnRef(ca_cert));
  EXPECT_CALL(config_, certificateRevocationList()).WillRepeatedly(ReturnRef(empty_string_));
  EXPECT_CALL(config_, trustChainVerification()).WillRepeatedly(Return(GetParam()));

  EXPECT_ENVOY_BUG(
      { PlatformBridgeCertValidator validator(&config_, stats_, &platform_validator_); },
      "Invalid certificate validation context config.");
}

TEST_P(PlatformBridgeCertValidatorTest, NonEmptyRevocationList) {
  std::string revocation_list = "xyz";
  EXPECT_CALL(config_, caCert()).WillRepeatedly(ReturnRef(empty_string_));
  EXPECT_CALL(config_, certificateRevocationList()).WillRepeatedly(ReturnRef(revocation_list));
  EXPECT_CALL(config_, trustChainVerification()).WillRepeatedly(Return(GetParam()));

  EXPECT_ENVOY_BUG(
      { PlatformBridgeCertValidator validator(&config_, stats_, &platform_validator_); },
      "Invalid certificate validation context config.");
}

TEST_P(PlatformBridgeCertValidatorTest, NoCallback) {
  initializeConfig();
  PlatformBridgeCertValidator validator(&config_, stats_, &platform_validator_);

  bssl::UniquePtr<STACK_OF(X509)> cert_chain = readCertChainFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns2_cert.pem"));
  std::string hostname = "www.example.com";

  EXPECT_ENVOY_BUG(
      {
        validator.doVerifyCertChain(*cert_chain, Ssl::ValidateResultCallbackPtr(),
                                    &ssl_extended_info_, transport_socket_options_, *ssl_ctx_,
                                    validation_context_, is_server_, hostname);
      },
      "No callback specified");
}

TEST_P(PlatformBridgeCertValidatorTest, EmptyCertChain) {
  initializeConfig();
  PlatformBridgeCertValidator validator(&config_, stats_, &platform_validator_);

  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  std::string hostname = "www.example.com";

  ValidationResults results = validator.doVerifyCertChain(
      *cert_chain, std::move(callback_), &ssl_extended_info_, transport_socket_options_, *ssl_ctx_,
      validation_context_, is_server_, hostname);
  EXPECT_EQ(ValidationResults::ValidationStatus::Failed, results.status);
  EXPECT_FALSE(results.tls_alert.has_value());
  ASSERT_TRUE(results.error_details.has_value());
  EXPECT_EQ("verify cert chain failed: empty cert chain.", results.error_details.value());
  EXPECT_EQ(Envoy::Ssl::ClientValidationStatus::NotValidated,
            ssl_extended_info_.certificateValidationStatus());
}

TEST_P(PlatformBridgeCertValidatorTest, ValidCertificate) {
  initializeConfig();
  PlatformBridgeCertValidator validator(&config_, stats_, &platform_validator_);

  std::string hostname = "server1.example.com";
  bssl::UniquePtr<STACK_OF(X509)> cert_chain = readCertChainFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns2_cert.pem"));
  envoy_cert_validation_result result = {ENVOY_SUCCESS, 0, NULL};
  EXPECT_CALL(*mock_validator_, validate(_, _, _)).WillOnce(Return(result));
  EXPECT_CALL(*mock_validator_, cleanup());
  auto& callback_ref = *callback_;
  EXPECT_CALL(callback_ref, dispatcher()).WillRepeatedly(ReturnRef(*dispatcher_));

  ValidationResults results = validator.doVerifyCertChain(
      *cert_chain, std::move(callback_), &ssl_extended_info_, transport_socket_options_, *ssl_ctx_,
      validation_context_, is_server_, hostname);
  EXPECT_EQ(ValidationResults::ValidationStatus::Pending, results.status);

  EXPECT_CALL(callback_ref, onCertValidationResult(true, "", 46)).WillOnce(Invoke([this]() {
    EXPECT_EQ(main_thread_id_, std::this_thread::get_id());
    dispatcher_->exit();
  }));
  EXPECT_FALSE(waitForDispatcherToExit());
}

TEST_P(PlatformBridgeCertValidatorTest, ValidCertificateButInvalidSni) {
  initializeConfig();
  PlatformBridgeCertValidator validator(&config_, stats_, &platform_validator_);

  std::string hostname = "server2.example.com";
  bssl::UniquePtr<STACK_OF(X509)> cert_chain = readCertChainFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns2_cert.pem"));
  envoy_cert_validation_result result = {ENVOY_SUCCESS, 0, NULL};
  EXPECT_CALL(*mock_validator_, validate(_, _, _)).WillOnce(Return(result));
  EXPECT_CALL(*mock_validator_, cleanup());
  auto& callback_ref = *callback_;
  EXPECT_CALL(callback_ref, dispatcher()).WillRepeatedly(ReturnRef(*dispatcher_));

  ValidationResults results = validator.doVerifyCertChain(
      *cert_chain, std::move(callback_), &ssl_extended_info_, transport_socket_options_, *ssl_ctx_,
      validation_context_, is_server_, hostname);
  EXPECT_EQ(ValidationResults::ValidationStatus::Pending, results.status);

  EXPECT_CALL(callback_ref,
              onCertValidationResult(
                  acceptInvalidCertificates(),
                  "PlatformBridgeCertValidator_verifySubjectAltName failed: SNI mismatch.",
                  SSL_AD_BAD_CERTIFICATE))
      .WillOnce(Invoke([this]() { dispatcher_->exit(); }));
  EXPECT_FALSE(waitForDispatcherToExit());
}

TEST_P(PlatformBridgeCertValidatorTest, ValidCertificateSniOverride) {
  initializeConfig();
  PlatformBridgeCertValidator validator(&config_, stats_, &platform_validator_);

  std::vector<std::string> subject_alt_names = {"server1.example.com"};

  std::string hostname = "server2.example.com";
  bssl::UniquePtr<STACK_OF(X509)> cert_chain = readCertChainFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns2_cert.pem"));
  envoy_cert_validation_result result = {ENVOY_SUCCESS, 0, NULL};
  EXPECT_CALL(*mock_validator_, validate(_, _, StrEq(subject_alt_names[0].c_str())))
      .WillOnce(Return(result));
  EXPECT_CALL(*mock_validator_, cleanup());
  auto& callback_ref = *callback_;
  EXPECT_CALL(callback_ref, dispatcher()).WillRepeatedly(ReturnRef(*dispatcher_));
  transport_socket_options_ =
      std::make_shared<Network::TransportSocketOptionsImpl>("", std::move(subject_alt_names));

  ValidationResults results = validator.doVerifyCertChain(
      *cert_chain, std::move(callback_), &ssl_extended_info_, transport_socket_options_, *ssl_ctx_,
      validation_context_, is_server_, hostname);
  EXPECT_EQ(ValidationResults::ValidationStatus::Pending, results.status);

  // The cert will be validated against the overridden name not the invalid name "server2".
  EXPECT_CALL(callback_ref, onCertValidationResult(true, "", 46)).WillOnce(Invoke([this]() {
    dispatcher_->exit();
  }));
  EXPECT_FALSE(waitForDispatcherToExit());
}

TEST_P(PlatformBridgeCertValidatorTest, DeletedWithValidationPending) {
  initializeConfig();
  auto validator =
      std::make_unique<PlatformBridgeCertValidator>(&config_, stats_, &platform_validator_);

  std::string hostname = "server1.example.com";
  bssl::UniquePtr<STACK_OF(X509)> cert_chain = readCertChainFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns2_cert.pem"));
  envoy_cert_validation_result result = {ENVOY_SUCCESS, 0, NULL};
  EXPECT_CALL(*mock_validator_, validate(_, _, _)).WillOnce(Return(result));
  EXPECT_CALL(*mock_validator_, cleanup());
  auto& callback_ref = *callback_;
  EXPECT_CALL(callback_ref, dispatcher()).WillRepeatedly(ReturnRef(*dispatcher_));

  ValidationResults results = validator->doVerifyCertChain(
      *cert_chain, std::move(callback_), &ssl_extended_info_, transport_socket_options_, *ssl_ctx_,
      validation_context_, is_server_, hostname);
  EXPECT_EQ(ValidationResults::ValidationStatus::Pending, results.status);

  validator.reset();

  // Since the validator was deleted, the callback should not be invoked and
  // so the dispatcher will not exit until the alarm fires.
  EXPECT_TRUE(waitForDispatcherToExit());
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
