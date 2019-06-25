#include <memory>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/common/exception.h"

#include "common/common/logger.h"
#include "common/secret/sds_api.h"
#include "common/secret/secret_manager_impl.h"
#include "common/ssl/certificate_validation_context_config_impl.h"
#include "common/ssl/tls_certificate_config_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Secret {
namespace {

class SecretManagerImplTest : public testing::Test, public Logger::Loggable<Logger::Id::secret> {
protected:
  SecretManagerImplTest() : api_(Api::createApiForTest()) {}

  void checkConfigDump(const std::string& expected_dump_yaml) {
    auto message_ptr = config_tracker_.config_tracker_callbacks_["secrets"]();
    const auto& secrets_config_dump =
        dynamic_cast<const envoy::admin::v2alpha::SecretsConfigDump&>(*message_ptr);
    envoy::admin::v2alpha::SecretsConfigDump expected_secrets_config_dump;
    TestUtility::loadFromYaml(expected_dump_yaml, expected_secrets_config_dump);
    EXPECT_EQ(expected_secrets_config_dump.DebugString(), secrets_config_dump.DebugString());
  }

  void setupSecretProviderContext() {}

  Api::ApiPtr api_;
  testing::NiceMock<Server::MockConfigTracker> config_tracker_;
  Event::SimulatedTimeSystem time_system_;
};

// Validate that secret manager adds static TLS certificate secret successfully.
TEST_F(SecretManagerImplTest, TlsCertificateSecretLoadSuccess) {
  envoy::api::v2::auth::Secret secret_config;
  const std::string yaml =
      R"EOF(
name: "abc.com"
tls_certificate:
  certificate_chain:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem"
)EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);
  std::unique_ptr<SecretManager> secret_manager(new SecretManagerImpl(config_tracker_));
  secret_manager->addStaticSecret(secret_config);

  ASSERT_EQ(secret_manager->findStaticTlsCertificateProvider("undefined"), nullptr);
  ASSERT_NE(secret_manager->findStaticTlsCertificateProvider("abc.com"), nullptr);

  Ssl::TlsCertificateConfigImpl tls_config(
      *secret_manager->findStaticTlsCertificateProvider("abc.com")->secret(), *api_);
  const std::string cert_pem =
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(cert_pem)),
            tls_config.certificateChain());

  const std::string key_pem =
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(key_pem)),
            tls_config.privateKey());
}

// Validate that secret manager throws an exception when adding duplicated static TLS certificate
// secret.
TEST_F(SecretManagerImplTest, DuplicateStaticTlsCertificateSecret) {
  envoy::api::v2::auth::Secret secret_config;
  const std::string yaml =
      R"EOF(
    name: "abc.com"
    tls_certificate:
      certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem"
    )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);
  std::unique_ptr<SecretManager> secret_manager(new SecretManagerImpl(config_tracker_));
  secret_manager->addStaticSecret(secret_config);

  ASSERT_NE(secret_manager->findStaticTlsCertificateProvider("abc.com"), nullptr);
  EXPECT_THROW_WITH_MESSAGE(secret_manager->addStaticSecret(secret_config), EnvoyException,
                            "Duplicate static TlsCertificate secret name abc.com");
}

// Validate that secret manager adds static certificate validation context secret successfully.
TEST_F(SecretManagerImplTest, CertificateValidationContextSecretLoadSuccess) {
  envoy::api::v2::auth::Secret secret_config;
  const std::string yaml =
      R"EOF(
      name: "abc.com"
      validation_context:
        trusted_ca: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem" }
        allow_expired_certificate: true
      )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);
  std::unique_ptr<SecretManager> secret_manager(new SecretManagerImpl(config_tracker_));
  secret_manager->addStaticSecret(secret_config);

  ASSERT_EQ(secret_manager->findStaticCertificateValidationContextProvider("undefined"), nullptr);
  ASSERT_NE(secret_manager->findStaticCertificateValidationContextProvider("abc.com"), nullptr);
  Ssl::CertificateValidationContextConfigImpl cvc_config(
      *secret_manager->findStaticCertificateValidationContextProvider("abc.com")->secret(), *api_);
  const std::string cert_pem =
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(cert_pem)),
            cvc_config.caCert());
}

// Validate that secret manager throws an exception when adding duplicated static certificate
// validation context secret.
TEST_F(SecretManagerImplTest, DuplicateStaticCertificateValidationContextSecret) {
  envoy::api::v2::auth::Secret secret_config;
  const std::string yaml =
      R"EOF(
    name: "abc.com"
    validation_context:
      trusted_ca: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem" }
      allow_expired_certificate: true
    )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);
  std::unique_ptr<SecretManager> secret_manager(new SecretManagerImpl(config_tracker_));
  secret_manager->addStaticSecret(secret_config);

  ASSERT_NE(secret_manager->findStaticCertificateValidationContextProvider("abc.com"), nullptr);
  EXPECT_THROW_WITH_MESSAGE(secret_manager->addStaticSecret(secret_config), EnvoyException,
                            "Duplicate static CertificateValidationContext secret name abc.com");
}

// Validate that secret manager throws an exception when adding static secret of a type that is not
// supported.
TEST_F(SecretManagerImplTest, NotImplementedException) {
  envoy::api::v2::auth::Secret secret_config;

  const std::string yaml =
      R"EOF(
name: "abc.com"
session_ticket_keys:
  keys:
    - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"
)EOF";

  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);

  std::unique_ptr<SecretManager> secret_manager(new SecretManagerImpl(config_tracker_));

  EXPECT_THROW_WITH_MESSAGE(secret_manager->addStaticSecret(secret_config), EnvoyException,
                            "Secret type not implemented");
}

TEST_F(SecretManagerImplTest, SdsDynamicSecretUpdateSuccess) {
  Server::MockInstance server;
  std::unique_ptr<SecretManager> secret_manager(new SecretManagerImpl(config_tracker_));

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> secret_context;

  envoy::api::v2::core::ConfigSource config_source;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockRandomGenerator> random;
  Stats::IsolatedStoreImpl stats;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Init::ExpectableWatcherImpl> init_watcher;
  Init::TargetHandlePtr init_target_handle;
  EXPECT_CALL(init_manager, add(_))
      .WillOnce(Invoke([&init_target_handle](const Init::Target& target) {
        init_target_handle = target.createHandle("test");
      }));
  EXPECT_CALL(secret_context, stats()).WillOnce(ReturnRef(stats));
  EXPECT_CALL(secret_context, initManager()).WillRepeatedly(Return(&init_manager));
  EXPECT_CALL(secret_context, dispatcher()).WillRepeatedly(ReturnRef(dispatcher));
  EXPECT_CALL(secret_context, localInfo()).WillOnce(ReturnRef(local_info));

  auto secret_provider =
      secret_manager->findOrCreateTlsCertificateProvider(config_source, "abc.com", secret_context);
  const std::string yaml =
      R"EOF(
name: "abc.com"
tls_certificate:
  certificate_chain:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem"
)EOF";
  envoy::api::v2::auth::Secret typed_secret;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), typed_secret);
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> secret_resources;
  secret_resources.Add()->PackFrom(typed_secret);
  init_target_handle->initialize(init_watcher);
  secret_context.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(secret_resources,
                                                                                   "");
  Ssl::TlsCertificateConfigImpl tls_config(*secret_provider->secret(), *api_);
  const std::string cert_pem =
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(cert_pem)),
            tls_config.certificateChain());
  const std::string key_pem =
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(key_pem)),
            tls_config.privateKey());
}

TEST_F(SecretManagerImplTest, ConfigDumpHandler) {
  Server::MockInstance server;
  auto secret_manager = std::make_unique<SecretManagerImpl>(config_tracker_);
  time_system_.setSystemTime(std::chrono::milliseconds(1234567891234));

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> secret_context;

  envoy::api::v2::core::ConfigSource config_source;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockRandomGenerator> random;
  Stats::IsolatedStoreImpl stats;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Init::ExpectableWatcherImpl> init_watcher;
  Init::TargetHandlePtr init_target_handle;
  EXPECT_CALL(init_manager, add(_))
      .WillRepeatedly(Invoke([&init_target_handle](const Init::Target& target) {
        init_target_handle = target.createHandle("test");
      }));
  EXPECT_CALL(secret_context, stats()).WillRepeatedly(ReturnRef(stats));
  EXPECT_CALL(secret_context, initManager()).WillRepeatedly(Return(&init_manager));
  EXPECT_CALL(secret_context, dispatcher()).WillRepeatedly(ReturnRef(dispatcher));
  EXPECT_CALL(secret_context, localInfo()).WillRepeatedly(ReturnRef(local_info));

  auto secret_provider =
      secret_manager->findOrCreateTlsCertificateProvider(config_source, "abc.com", secret_context);
  const std::string yaml =
      R"EOF(
name: "abc.com"
tls_certificate:
  certificate_chain:
    inline_string: "DUMMY_INLINE_BYTES_FOR_CERT_CHAIN"
  private_key:
    inline_string: "DUMMY_INLINE_BYTES_FOR_PRIVATE_KEY"
)EOF";
  envoy::api::v2::auth::Secret typed_secret;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), typed_secret);
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> secret_resources;
  secret_resources.Add()->PackFrom(typed_secret);
  init_target_handle->initialize(init_watcher);
  secret_context.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(secret_resources,
                                                                                   "keycert-v1");
  Ssl::TlsCertificateConfigImpl tls_config(*secret_provider->secret(), *api_);
  EXPECT_EQ("DUMMY_INLINE_BYTES_FOR_CERT_CHAIN", tls_config.certificateChain());
  EXPECT_EQ("DUMMY_INLINE_BYTES_FOR_PRIVATE_KEY", tls_config.privateKey());

  // Private key is removed.
  const std::string expected_secrets_config_dump = R"EOF(
dynamic_secrets:
  version_info: "keycert-v1"
  last_updated:
    seconds: 1234567891
    nanos: 234000000
  secret:
    name: "abc.com"
    tls_certificate:
      certificate_chain:
        inline_string: "DUMMY_INLINE_BYTES_FOR_CERT_CHAIN"
)EOF";
  checkConfigDump(expected_secrets_config_dump);

  // Add a dynamic tls validation context provider.
  time_system_.setSystemTime(std::chrono::milliseconds(1234567899000));
  auto context_secret_provider = secret_manager->findOrCreateCertificateValidationContextProvider(
      config_source, "abc.com.validation", secret_context);
  const std::string validation_yaml = R"EOF(
name: "abc.com.validation"
validation_context:
  trusted_ca:
    inline_string: "DUMMY_INLINE_STRING_TRUSTED_CA" 
)EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(validation_yaml), typed_secret);
  secret_resources.Clear();
  secret_resources.Add()->PackFrom(typed_secret);

  // TODO(incfly): different config source is needed here...
  // add helper function in the test class for easier add the test.
  // uint64_t hash = MessageUtil::hash(config_source);
  init_target_handle->initialize(init_watcher);
  secret_context.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      secret_resources, "validation-context-v1");
  Ssl::CertificateValidationContextConfigImpl cert_validation_context(
      *context_secret_provider->secret(), *api_);
  EXPECT_EQ("DUMMY_INLINE_STRING_TRUSTED_CA", cert_validation_context.caCert());
  const std::string updated_config_dump = R"EOF(
dynamic_secrets:
- version_info: "keycert-v1"
  last_updated:
    seconds: 1234567891
    nanos: 234000000
  secret:
    name: "abc.com"
    tls_certificate:
      certificate_chain:
        inline_string: "DUMMY_INLINE_BYTES_FOR_CERT_CHAIN"
- version_info: "validation-context-v1"
  last_updated:
    seconds: 1234567899
  secret:
    name: "abc.com.validation"
    validation_context:
      trusted_ca:
        inline_string: "DUMMY_INLINE_STRING_TRUSTED_CA" 
)EOF";
  checkConfigDump(updated_config_dump);
}

} // namespace
} // namespace Secret
} // namespace Envoy
