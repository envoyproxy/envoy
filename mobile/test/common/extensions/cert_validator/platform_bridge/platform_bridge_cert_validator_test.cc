#include <memory>
#include <string>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/crypto/utility.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/tls/cert_validator/default_validator.h"
#include "source/common/tls/cert_validator/san_matcher.h"
#include "source/common/tls/stats.h"

#include "test/common/mocks/common/mocks.h"
#include "test/common/stats/stat_test_utility.h"
#include "test/common/tls/cert_validator/test_common.h"
#include "test/common/tls/ssl_test_utility.h"
#include "test/common/tls/test_data/san_dns2_cert_info.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/thread/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "library/common/bridge/utility.h"
#include "library/common/extensions/cert_validator/platform_bridge/config.h"
#include "library/common/extensions/cert_validator/platform_bridge/platform_bridge.pb.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

using SSLContextPtr = Envoy::CSmartPtr<SSL_CTX, SSL_CTX_free>;

using envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext;

using testing::_;
using testing::ByMove;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrEq;
using testing::WithArgs;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class MockValidateResultCallback : public Ssl::ValidateResultCallback {
public:
  ~MockValidateResultCallback() override = default;

  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(void, onCertValidationResult,
              (bool, Envoy::Ssl::ClientValidationStatus, const std::string&, uint8_t));
};

class MockValidator {
public:
  MOCK_METHOD(void, cleanup, ());
  MOCK_METHOD(envoy_cert_validation_result, validate,
              (const std::vector<std::string>& certs, absl::string_view hostname));
};

class PlatformBridgeCertValidatorCustomValidate : public PlatformBridgeCertValidator {
public:
  PlatformBridgeCertValidatorCustomValidate(
      const Envoy::Ssl::CertificateValidationContextConfig* config, SslStats& stats,
      Thread::PosixThreadFactory& thread_factory)
      : PlatformBridgeCertValidator(config, stats), thread_factory_(thread_factory) {}

  int recordedThreadPriority() const { return recorded_thread_priority_; }

protected:
  void verifyCertChainByPlatform(Event::Dispatcher* dispatcher,
                                 std::vector<std::string> /* cert_chain */, std::string hostname,
                                 std::vector<std::string> /* subject_alt_names */) override {
    recorded_thread_priority_ = thread_factory_.currentThreadPriority();
    postVerifyResultAndCleanUp(/* success = */ true, std::move(hostname), "",
                               SSL_AD_CERTIFICATE_UNKNOWN, ValidationFailureType::Success,
                               dispatcher, this);
  }

private:
  Thread::PosixThreadFactory& thread_factory_;
  int recorded_thread_priority_;
};

class PlatformBridgeCertValidatorTest
    : public testing::TestWithParam<CertificateValidationContext::TrustChainVerification> {
protected:
  PlatformBridgeCertValidatorTest()
      : thread_factory_(Thread::PosixThreadFactory::create()), api_(Api::createApiForTest()),
        dispatcher_(api_->allocateDispatcher("test_thread")),
        stats_(generateSslStats(*test_store_.rootScope())), ssl_ctx_(SSL_CTX_new(TLS_method())),
        callback_(std::make_unique<MockValidateResultCallback>()), is_server_(false),
        mock_validator_(std::make_unique<MockValidator>()),
        main_thread_id_(thread_factory_->currentPthreadId()),
        helper_handle_(test::SystemHelperPeer::replaceSystemHelper()) {
    ON_CALL(helper_handle_->mock_helper(), validateCertificateChain(_, _))
        .WillByDefault(WithArgs<0, 1>(Invoke(this, &PlatformBridgeCertValidatorTest::validate)));
    ON_CALL(helper_handle_->mock_helper(), cleanupAfterCertificateValidation())
        .WillByDefault(Invoke(this, &PlatformBridgeCertValidatorTest::cleanup));

    EXPECT_CALL(helper_handle_->mock_helper(), isCleartextPermitted(_))
        .WillRepeatedly(Return(true));
  }

  void initializeConfig() {
    EXPECT_CALL(config_, caCert()).WillOnce(ReturnRef(empty_string_));
    EXPECT_CALL(config_, certificateRevocationList()).WillOnce(ReturnRef(empty_string_));
    EXPECT_CALL(config_, trustChainVerification()).WillOnce(Return(GetParam()));
    EXPECT_CALL(config_, customValidatorConfig())
        .WillRepeatedly(ReturnRef(platform_bridge_config_));
  }

  ~PlatformBridgeCertValidatorTest() {
    mock_validator_.reset();
    main_thread_id_ = thread_factory_->currentPthreadId();
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

  envoy_cert_validation_result validate(const std::vector<std::string>& certs,
                                        absl::string_view hostname) {
    // Validate must be called on the worker thread, not the main thread.
    EXPECT_NE(main_thread_id_, thread_factory_->currentPthreadId());

    // Make sure the cert was converted correctly.
    const Buffer::InstancePtr buffer(new Buffer::OwnedImpl(certs[0]));
    const auto digest = Common::Crypto::UtilitySingleton::get().getSha256Digest(*buffer);
    EXPECT_EQ(TEST_SAN_DNS2_CERT_256_HASH, Hex::encode(digest));
    return mock_validator_->validate(certs, hostname);
  }

  void cleanup() {
    // Validate must be called on the worker thread, not the main thread.
    EXPECT_NE(main_thread_id_, thread_factory_->currentPthreadId());
    mock_validator_->cleanup();
  }

  Thread::PosixThreadFactoryPtr thread_factory_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Stats::TestUtil::TestStore test_store_;
  SslStats stats_;
  Ssl::MockCertificateValidationContextConfig config_;
  std::string empty_string_;
  SSLContextPtr ssl_ctx_;
  CertValidator::ExtraValidationContext validation_context_;
  Network::TransportSocketOptionsConstSharedPtr transport_socket_options_;
  std::unique_ptr<MockValidateResultCallback> callback_;
  bool is_server_;
  std::unique_ptr<MockValidator> mock_validator_;
  Thread::ThreadId main_thread_id_;
  std::unique_ptr<test::SystemHelperPeer::Handle> helper_handle_;
  absl::optional<envoy::config::core::v3::TypedExtensionConfig> platform_bridge_config_;
};

INSTANTIATE_TEST_SUITE_P(TrustMode, PlatformBridgeCertValidatorTest,
                         testing::ValuesIn({CertificateValidationContext::VERIFY_TRUST_CHAIN,
                                            CertificateValidationContext::ACCEPT_UNTRUSTED}));

TEST_P(PlatformBridgeCertValidatorTest, NoConfig) {
  EXPECT_ENVOY_BUG({ PlatformBridgeCertValidator validator(nullptr, stats_); },
                   "Invalid certificate validation context config.");
}

TEST_P(PlatformBridgeCertValidatorTest, NonEmptyCaCert) {
  std::string ca_cert = "xyz";
  EXPECT_CALL(config_, caCert()).WillRepeatedly(ReturnRef(ca_cert));
  EXPECT_CALL(config_, certificateRevocationList()).WillRepeatedly(ReturnRef(empty_string_));
  EXPECT_CALL(config_, trustChainVerification()).WillRepeatedly(Return(GetParam()));
  EXPECT_CALL(config_, customValidatorConfig()).WillRepeatedly(ReturnRef(platform_bridge_config_));

  EXPECT_ENVOY_BUG({ PlatformBridgeCertValidator validator(&config_, stats_); },
                   "Invalid certificate validation context config.");
}

TEST_P(PlatformBridgeCertValidatorTest, NonEmptyRevocationList) {
  std::string revocation_list = "xyz";
  EXPECT_CALL(config_, caCert()).WillRepeatedly(ReturnRef(empty_string_));
  EXPECT_CALL(config_, certificateRevocationList()).WillRepeatedly(ReturnRef(revocation_list));
  EXPECT_CALL(config_, trustChainVerification()).WillRepeatedly(Return(GetParam()));
  EXPECT_CALL(config_, customValidatorConfig()).WillRepeatedly(ReturnRef(platform_bridge_config_));

  EXPECT_ENVOY_BUG({ PlatformBridgeCertValidator validator(&config_, stats_); },
                   "Invalid certificate validation context config.");
}

TEST_P(PlatformBridgeCertValidatorTest, NoCallback) {
  initializeConfig();
  PlatformBridgeCertValidator validator(&config_, stats_);

  bssl::UniquePtr<STACK_OF(X509)> cert_chain = readCertChainFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns2_cert.pem"));
  std::string hostname = "www.example.com";

  EXPECT_ENVOY_BUG(
      {
        validator.doVerifyCertChain(*cert_chain, Ssl::ValidateResultCallbackPtr(),
                                    transport_socket_options_, *ssl_ctx_, validation_context_,
                                    is_server_, hostname);
      },
      "No callback specified");
}

TEST_P(PlatformBridgeCertValidatorTest, EmptyCertChain) {
  initializeConfig();
  PlatformBridgeCertValidator validator(&config_, stats_);

  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  std::string hostname = "www.example.com";

  ValidationResults results =
      validator.doVerifyCertChain(*cert_chain, std::move(callback_), transport_socket_options_,
                                  *ssl_ctx_, validation_context_, is_server_, hostname);
  EXPECT_EQ(ValidationResults::ValidationStatus::Failed, results.status);
  EXPECT_FALSE(results.tls_alert.has_value());
  ASSERT_TRUE(results.error_details.has_value());
  EXPECT_EQ("verify cert chain failed: empty cert chain.", results.error_details.value());
  EXPECT_EQ(Envoy::Ssl::ClientValidationStatus::NotValidated, results.detailed_status);
}

TEST_P(PlatformBridgeCertValidatorTest, ValidCertificate) {
  EXPECT_CALL(helper_handle_->mock_helper(), cleanupAfterCertificateValidation());
  EXPECT_CALL(helper_handle_->mock_helper(), validateCertificateChain(_, _));

  initializeConfig();
  PlatformBridgeCertValidator validator(&config_, stats_);

  std::string hostname = "server1.example.com";
  bssl::UniquePtr<STACK_OF(X509)> cert_chain = readCertChainFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns2_cert.pem"));
  envoy_cert_validation_result result = {ENVOY_SUCCESS, 0, NULL};
  EXPECT_CALL(*mock_validator_, validate(_, _)).WillOnce(Return(result));
  EXPECT_CALL(*mock_validator_, cleanup());
  auto& callback_ref = *callback_;
  EXPECT_CALL(callback_ref, dispatcher()).WillRepeatedly(ReturnRef(*dispatcher_));

  ValidationResults results =
      validator.doVerifyCertChain(*cert_chain, std::move(callback_), transport_socket_options_,
                                  *ssl_ctx_, validation_context_, is_server_, hostname);
  EXPECT_EQ(ValidationResults::ValidationStatus::Pending, results.status);

  EXPECT_CALL(callback_ref,
              onCertValidationResult(true, Envoy::Ssl::ClientValidationStatus::Validated, "", 46))
      .WillOnce(Invoke([this]() {
        EXPECT_EQ(main_thread_id_, thread_factory_->currentPthreadId());
        dispatcher_->exit();
      }));
  EXPECT_FALSE(waitForDispatcherToExit());
}

TEST_P(PlatformBridgeCertValidatorTest, ValidCertificateEmptySanOverrides) {
  EXPECT_CALL(helper_handle_->mock_helper(), cleanupAfterCertificateValidation());
  EXPECT_CALL(helper_handle_->mock_helper(), validateCertificateChain(_, _));

  initializeConfig();
  PlatformBridgeCertValidator validator(&config_, stats_);

  std::string hostname = "server1.example.com";
  bssl::UniquePtr<STACK_OF(X509)> cert_chain = readCertChainFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns2_cert.pem"));
  envoy_cert_validation_result result = {ENVOY_SUCCESS, 0, NULL};
  EXPECT_CALL(*mock_validator_, validate(_, _)).WillOnce(Return(result));
  EXPECT_CALL(*mock_validator_, cleanup());
  auto& callback_ref = *callback_;
  EXPECT_CALL(callback_ref, dispatcher()).WillRepeatedly(ReturnRef(*dispatcher_));

  // Set up transport socket options with an empty SAN list.
  std::vector<std::string> subject_alt_names;
  transport_socket_options_ =
      std::make_shared<Network::TransportSocketOptionsImpl>("", std::move(subject_alt_names));

  ValidationResults results =
      validator.doVerifyCertChain(*cert_chain, std::move(callback_), transport_socket_options_,
                                  *ssl_ctx_, validation_context_, is_server_, hostname);
  EXPECT_EQ(ValidationResults::ValidationStatus::Pending, results.status);

  EXPECT_CALL(callback_ref,
              onCertValidationResult(true, Envoy::Ssl::ClientValidationStatus::Validated, "", 46))
      .WillOnce(Invoke([this]() {
        EXPECT_EQ(main_thread_id_, thread_factory_->currentPthreadId());
        dispatcher_->exit();
      }));
  EXPECT_FALSE(waitForDispatcherToExit());
}

TEST_P(PlatformBridgeCertValidatorTest, ValidCertificateEmptyHostNoOverrides) {
  EXPECT_CALL(helper_handle_->mock_helper(), cleanupAfterCertificateValidation());
  EXPECT_CALL(helper_handle_->mock_helper(), validateCertificateChain(_, _));

  initializeConfig();
  PlatformBridgeCertValidator validator(&config_, stats_);

  std::string hostname = "";
  bssl::UniquePtr<STACK_OF(X509)> cert_chain = readCertChainFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns2_cert.pem"));
  envoy_cert_validation_result result = {ENVOY_SUCCESS, 0, NULL};
  EXPECT_CALL(*mock_validator_, validate(_, _)).WillOnce(Return(result));
  EXPECT_CALL(*mock_validator_, cleanup());
  auto& callback_ref = *callback_;
  EXPECT_CALL(callback_ref, dispatcher()).WillRepeatedly(ReturnRef(*dispatcher_));

  // Set up transport socket options with an empty SAN list.
  std::vector<std::string> subject_alt_names;
  transport_socket_options_ =
      std::make_shared<Network::TransportSocketOptionsImpl>("", std::move(subject_alt_names));

  ValidationResults results =
      validator.doVerifyCertChain(*cert_chain, std::move(callback_), transport_socket_options_,
                                  *ssl_ctx_, validation_context_, is_server_, hostname);
  EXPECT_EQ(ValidationResults::ValidationStatus::Pending, results.status);

  EXPECT_CALL(callback_ref,
              onCertValidationResult(true, Envoy::Ssl::ClientValidationStatus::Validated, "", 46))
      .WillOnce(Invoke([this]() {
        EXPECT_EQ(main_thread_id_, thread_factory_->currentPthreadId());
        dispatcher_->exit();
      }));
  EXPECT_FALSE(waitForDispatcherToExit());
}

TEST_P(PlatformBridgeCertValidatorTest, ValidCertificateButInvalidSni) {
  EXPECT_CALL(helper_handle_->mock_helper(), cleanupAfterCertificateValidation());
  EXPECT_CALL(helper_handle_->mock_helper(), validateCertificateChain(_, _));

  initializeConfig();
  PlatformBridgeCertValidator validator(&config_, stats_);

  std::string hostname = "server2.example.com";
  bssl::UniquePtr<STACK_OF(X509)> cert_chain = readCertChainFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns2_cert.pem"));
  envoy_cert_validation_result result = {ENVOY_SUCCESS, 0, NULL};
  EXPECT_CALL(*mock_validator_, validate(_, _)).WillOnce(Return(result));
  EXPECT_CALL(*mock_validator_, cleanup());
  auto& callback_ref = *callback_;
  EXPECT_CALL(callback_ref, dispatcher()).WillRepeatedly(ReturnRef(*dispatcher_));

  ValidationResults results =
      validator.doVerifyCertChain(*cert_chain, std::move(callback_), transport_socket_options_,
                                  *ssl_ctx_, validation_context_, is_server_, hostname);
  EXPECT_EQ(ValidationResults::ValidationStatus::Pending, results.status);

  EXPECT_CALL(callback_ref,
              onCertValidationResult(
                  acceptInvalidCertificates(), Envoy::Ssl::ClientValidationStatus::Failed,
                  "PlatformBridgeCertValidator_verifySubjectAltName failed: SNI mismatch.",
                  SSL_AD_BAD_CERTIFICATE))
      .WillOnce(Invoke([this]() { dispatcher_->exit(); }));
  EXPECT_FALSE(waitForDispatcherToExit());
}

TEST_P(PlatformBridgeCertValidatorTest, ValidCertificateSniOverride) {
  EXPECT_CALL(helper_handle_->mock_helper(), cleanupAfterCertificateValidation());
  EXPECT_CALL(helper_handle_->mock_helper(), validateCertificateChain(_, _));

  initializeConfig();
  PlatformBridgeCertValidator validator(&config_, stats_);

  std::vector<std::string> subject_alt_names = {"server1.example.com"};

  std::string hostname = "server2.example.com";
  bssl::UniquePtr<STACK_OF(X509)> cert_chain = readCertChainFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns2_cert.pem"));
  envoy_cert_validation_result result = {ENVOY_SUCCESS, 0, NULL};
  EXPECT_CALL(*mock_validator_, validate(_, StrEq(subject_alt_names[0].c_str())))
      .WillOnce(Return(result));
  EXPECT_CALL(*mock_validator_, cleanup());
  auto& callback_ref = *callback_;
  EXPECT_CALL(callback_ref, dispatcher()).WillRepeatedly(ReturnRef(*dispatcher_));
  transport_socket_options_ =
      std::make_shared<Network::TransportSocketOptionsImpl>("", std::move(subject_alt_names));

  ValidationResults results =
      validator.doVerifyCertChain(*cert_chain, std::move(callback_), transport_socket_options_,
                                  *ssl_ctx_, validation_context_, is_server_, hostname);
  EXPECT_EQ(ValidationResults::ValidationStatus::Pending, results.status);

  // The cert will be validated against the overridden name not the invalid name "server2".
  EXPECT_CALL(callback_ref,
              onCertValidationResult(true, Envoy::Ssl::ClientValidationStatus::Validated, "", 46))
      .WillOnce(Invoke([this]() { dispatcher_->exit(); }));
  EXPECT_FALSE(waitForDispatcherToExit());
}

TEST_P(PlatformBridgeCertValidatorTest, DeletedWithValidationPending) {
  EXPECT_CALL(helper_handle_->mock_helper(), cleanupAfterCertificateValidation());
  EXPECT_CALL(helper_handle_->mock_helper(), validateCertificateChain(_, _));

  initializeConfig();
  auto validator = std::make_unique<PlatformBridgeCertValidator>(&config_, stats_);

  std::string hostname = "server1.example.com";
  bssl::UniquePtr<STACK_OF(X509)> cert_chain = readCertChainFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns2_cert.pem"));
  envoy_cert_validation_result result = {ENVOY_SUCCESS, 0, NULL};
  EXPECT_CALL(*mock_validator_, validate(_, _)).WillOnce(Return(result));
  EXPECT_CALL(*mock_validator_, cleanup());
  auto& callback_ref = *callback_;
  EXPECT_CALL(callback_ref, dispatcher()).WillRepeatedly(ReturnRef(*dispatcher_));

  ValidationResults results =
      validator->doVerifyCertChain(*cert_chain, std::move(callback_), transport_socket_options_,
                                   *ssl_ctx_, validation_context_, is_server_, hostname);
  EXPECT_EQ(ValidationResults::ValidationStatus::Pending, results.status);

  validator.reset();

  // Since the validator was deleted, the callback should not be invoked and
  // so the dispatcher will not exit until the alarm fires.
  EXPECT_TRUE(waitForDispatcherToExit());
}

TEST_P(PlatformBridgeCertValidatorTest, ThreadCreationFailed) {
  initializeConfig();
  auto thread_factory = std::make_unique<Thread::MockPosixThreadFactory>();
  EXPECT_CALL(*thread_factory, createThread(_, _, false)).WillOnce(Return(ByMove(nullptr)));
  PlatformBridgeCertValidator validator(&config_, stats_, std::move(thread_factory));

  std::string hostname = "server1.example.com";
  bssl::UniquePtr<STACK_OF(X509)> cert_chain = readCertChainFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns2_cert.pem"));
  auto& callback_ref = *callback_;
  EXPECT_CALL(callback_ref, dispatcher()).WillRepeatedly(ReturnRef(*dispatcher_));

  ValidationResults results =
      validator.doVerifyCertChain(*cert_chain, std::move(callback_), transport_socket_options_,
                                  *ssl_ctx_, validation_context_, is_server_, hostname);
  EXPECT_EQ(ValidationResults::ValidationStatus::Failed, results.status);
  EXPECT_EQ(Ssl::ClientValidationStatus::NotValidated, results.detailed_status);
  EXPECT_EQ("Failed creating a thread for cert chain validation.", *results.error_details);
}

TEST_P(PlatformBridgeCertValidatorTest, ThreadPriority) {
  const int expected_thread_priority = 15;
  envoy_mobile::extensions::cert_validator::platform_bridge::PlatformBridgeCertValidator
      platform_bridge_config;
  platform_bridge_config.mutable_thread_priority()->set_value(expected_thread_priority);
  envoy::config::core::v3::TypedExtensionConfig typed_config;
  typed_config.set_name("PlatformBridgeCertValidator");
  typed_config.mutable_typed_config()->PackFrom(platform_bridge_config);
  platform_bridge_config_ = std::move(typed_config);

  EXPECT_CALL(helper_handle_->mock_helper(), cleanupAfterCertificateValidation());

  initializeConfig();
  PlatformBridgeCertValidatorCustomValidate validator(&config_, stats_, *thread_factory_);

  std::string hostname = "server1.example.com";
  bssl::UniquePtr<STACK_OF(X509)> cert_chain = readCertChainFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns2_cert.pem"));
  EXPECT_CALL(*mock_validator_, cleanup());
  auto& callback_ref = *callback_;
  EXPECT_CALL(callback_ref, dispatcher()).WillRepeatedly(ReturnRef(*dispatcher_));

  ValidationResults results =
      validator.doVerifyCertChain(*cert_chain, std::move(callback_), transport_socket_options_,
                                  *ssl_ctx_, validation_context_, is_server_, hostname);
  EXPECT_EQ(ValidationResults::ValidationStatus::Pending, results.status);

  EXPECT_CALL(callback_ref,
              onCertValidationResult(true, Envoy::Ssl::ClientValidationStatus::Validated, "", 46))
      .WillOnce(Invoke([this, &validator, expected_thread_priority]() {
        EXPECT_EQ(validator.recordedThreadPriority(), expected_thread_priority);
        dispatcher_->exit();
      }));
  EXPECT_FALSE(waitForDispatcherToExit());
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
