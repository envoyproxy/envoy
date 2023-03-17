#include <memory>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/common/exception.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/grpc_credential/v2alpha/file_based_metadata.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/common/common/base64.h"
#include "source/common/common/logger.h"
#include "source/common/config/api_version.h"
#include "source/common/secret/sds_api.h"
#include "source/common/secret/secret_manager_impl.h"
#include "source/common/ssl/certificate_validation_context_config_impl.h"
#include "source/common/ssl/tls_certificate_config_impl.h"

#include "test/common/secret/private_key_provider.pb.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/matcher/mocks.h"
#include "test/mocks/server/config_tracker.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ReturnRef;
using testing::StrictMock;

namespace Envoy {
namespace Secret {
namespace {

const ::test::common::secret::TestPrivateKeyMethodConfig _mock_test_private_key_method_config_dummy;
using ::Envoy::Matchers::MockStringMatcher;

class SecretManagerImplTest : public testing::Test, public Logger::Loggable<Logger::Id::secret> {
protected:
  SecretManagerImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  void checkConfigDump(
      const std::string& expected_dump_yaml,
      const Matchers::StringMatcher& name_matcher = Matchers::UniversalStringMatcher()) {
    auto message_ptr = config_tracker_.config_tracker_callbacks_["secrets"](name_matcher);
    const auto& secrets_config_dump =
        dynamic_cast<const envoy::admin::v3::SecretsConfigDump&>(*message_ptr);
    envoy::admin::v3::SecretsConfigDump expected_secrets_config_dump;
    TestUtility::loadFromYaml(expected_dump_yaml, expected_secrets_config_dump);
    EXPECT_THAT(secrets_config_dump,
                ProtoEqIgnoreRepeatedFieldOrdering(expected_secrets_config_dump));
  }

  void setupSecretProviderContext() {}

  Api::ApiPtr api_;
  testing::NiceMock<Server::MockConfigTracker> config_tracker_;
  Event::SimulatedTimeSystem time_system_;
  Event::DispatcherPtr dispatcher_;
};

// Validate that secret manager adds static TLS certificate secret successfully.
TEST_F(SecretManagerImplTest, TlsCertificateSecretLoadSuccess) {
  envoy::extensions::transport_sockets::tls::v3::Secret secret_config;
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
  SecretManagerPtr secret_manager(new SecretManagerImpl(config_tracker_));
  secret_manager->addStaticSecret(secret_config);

  ASSERT_EQ(secret_manager->findStaticTlsCertificateProvider("undefined"), nullptr);
  ASSERT_NE(secret_manager->findStaticTlsCertificateProvider("abc.com"), nullptr);

  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> ctx;
  Ssl::TlsCertificateConfigImpl tls_config(
      *secret_manager->findStaticTlsCertificateProvider("abc.com")->secret(), ctx, *api_);
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
  envoy::extensions::transport_sockets::tls::v3::Secret secret_config;
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
  SecretManagerPtr secret_manager(new SecretManagerImpl(config_tracker_));
  secret_manager->addStaticSecret(secret_config);

  ASSERT_NE(secret_manager->findStaticTlsCertificateProvider("abc.com"), nullptr);
  EXPECT_THROW_WITH_MESSAGE(secret_manager->addStaticSecret(secret_config), EnvoyException,
                            "Duplicate static TlsCertificate secret name abc.com");
}

// Validate that secret manager adds static certificate validation context secret successfully.
TEST_F(SecretManagerImplTest, CertificateValidationContextSecretLoadSuccess) {
  envoy::extensions::transport_sockets::tls::v3::Secret secret_config;
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
  envoy::extensions::transport_sockets::tls::v3::Secret secret_config;
  const std::string yaml =
      R"EOF(
    name: "abc.com"
    validation_context:
      trusted_ca: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem" }
      allow_expired_certificate: true
    )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);
  SecretManagerPtr secret_manager(new SecretManagerImpl(config_tracker_));
  secret_manager->addStaticSecret(secret_config);

  ASSERT_NE(secret_manager->findStaticCertificateValidationContextProvider("abc.com"), nullptr);
  EXPECT_THROW_WITH_MESSAGE(secret_manager->addStaticSecret(secret_config), EnvoyException,
                            "Duplicate static CertificateValidationContext secret name abc.com");
}

// Validate that secret manager adds static STKs secret successfully.
TEST_F(SecretManagerImplTest, SessionTicketKeysLoadSuccess) {
  envoy::extensions::transport_sockets::tls::v3::Secret secret_config;

  const std::string yaml =
      R"EOF(
name: "abc.com"
session_ticket_keys:
  keys:
    - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/keys.bin"
)EOF";

  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);

  SecretManagerPtr secret_manager(new SecretManagerImpl(config_tracker_));

  secret_manager->addStaticSecret(secret_config);

  ASSERT_EQ(secret_manager->findStaticTlsSessionTicketKeysContextProvider("undefined"), nullptr);
  ASSERT_NE(secret_manager->findStaticTlsSessionTicketKeysContextProvider("abc.com"), nullptr);

  const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys session_ticket_keys(
      *secret_manager->findStaticTlsSessionTicketKeysContextProvider("abc.com")->secret());
  const std::string keys_path =
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/keys.bin";
  EXPECT_EQ(session_ticket_keys.keys_size(), 1);
  EXPECT_EQ(session_ticket_keys.keys()[0].filename(), TestEnvironment::substitute(keys_path));
}

// Validate that secret manager throws an exception when adding duplicated static STKs secret.
TEST_F(SecretManagerImplTest, DuplicateSessionTicketKeysSecret) {
  envoy::extensions::transport_sockets::tls::v3::Secret secret_config;

  const std::string yaml =
      R"EOF(
name: "abc.com"
session_ticket_keys:
  keys:
    - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/keys.bin"
)EOF";

  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);

  SecretManagerPtr secret_manager(new SecretManagerImpl(config_tracker_));

  secret_manager->addStaticSecret(secret_config);

  ASSERT_NE(secret_manager->findStaticTlsSessionTicketKeysContextProvider("abc.com"), nullptr);
  EXPECT_THROW_WITH_MESSAGE(secret_manager->addStaticSecret(secret_config), EnvoyException,
                            "Duplicate static TlsSessionTicketKeys secret name abc.com");
}

// Validate that secret manager adds static generic secret successfully.
TEST_F(SecretManagerImplTest, GenericSecretLoadSuccess) {
  SecretManagerPtr secret_manager(new SecretManagerImpl(config_tracker_));

  envoy::extensions::transport_sockets::tls::v3::Secret secret;
  const std::string yaml =
      R"EOF(
name: "encryption_key"
generic_secret:
  secret:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/aes_128_key"
)EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), secret);
  secret_manager->addStaticSecret(secret);

  ASSERT_EQ(secret_manager->findStaticGenericSecretProvider("undefined"), nullptr);
  ASSERT_NE(secret_manager->findStaticGenericSecretProvider("encryption_key"), nullptr);

  const envoy::extensions::transport_sockets::tls::v3::GenericSecret generic_secret(
      *secret_manager->findStaticGenericSecretProvider("encryption_key")->secret());
  const std::string secret_path =
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/aes_128_key";
  EXPECT_EQ(generic_secret.secret().filename(), TestEnvironment::substitute(secret_path));
}

// Validate that secret manager throws an exception when adding duplicated static generic secret.
TEST_F(SecretManagerImplTest, DuplicateGenericSecret) {
  SecretManagerPtr secret_manager(new SecretManagerImpl(config_tracker_));

  envoy::extensions::transport_sockets::tls::v3::Secret secret;
  const std::string yaml =
      R"EOF(
name: "encryption_key"
generic_secret:
  secret:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/aes_128_key"
)EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), secret);
  secret_manager->addStaticSecret(secret);

  ASSERT_NE(secret_manager->findStaticGenericSecretProvider("encryption_key"), nullptr);
  EXPECT_THROW_WITH_MESSAGE(secret_manager->addStaticSecret(secret), EnvoyException,
                            "Duplicate static GenericSecret secret name encryption_key");
}

// Validate that secret manager deduplicates dynamic TLS certificate secret provider.
// Regression test of https://github.com/envoyproxy/envoy/issues/5744
TEST_F(SecretManagerImplTest, DeduplicateDynamicTlsCertificateSecretProvider) {
  Server::MockInstance server;
  SecretManagerPtr secret_manager(new SecretManagerImpl(config_tracker_));

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> secret_context;

  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Random::MockRandomGenerator> random;
  Stats::IsolatedStoreImpl stats;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Init::ExpectableWatcherImpl> init_watcher;
  Init::TargetHandlePtr init_target_handle;
  EXPECT_CALL(init_manager, add(_))
      .WillRepeatedly(Invoke([&init_target_handle](const Init::Target& target) {
        init_target_handle = target.createHandle("test");
      }));
  EXPECT_CALL(secret_context, stats()).WillRepeatedly(ReturnRef(stats));
  EXPECT_CALL(secret_context, initManager()).Times(0);
  EXPECT_CALL(secret_context, mainThreadDispatcher()).WillRepeatedly(ReturnRef(dispatcher));
  EXPECT_CALL(secret_context, localInfo()).WillRepeatedly(ReturnRef(local_info));

  envoy::config::core::v3::ConfigSource config_source;
  TestUtility::loadFromYaml(R"(
api_config_source:
  api_type: GRPC
  grpc_services:
  - google_grpc:
      call_credentials:
      - from_plugin:
          name: file_based_metadata
          typed_config:
            "@type": type.googleapis.com/envoy.config.grpc_credential.v2alpha.FileBasedMetadataConfig
      stat_prefix: sdsstat
      credentials_factory_name: envoy.grpc_credentials.file_based_metadata
  )",
                            config_source);
  config_source.mutable_api_config_source()
      ->mutable_grpc_services(0)
      ->mutable_google_grpc()
      ->mutable_call_credentials(0)
      ->mutable_from_plugin()
      ->mutable_typed_config()
      ->set_value(Base64::decode("CjUKMy92YXIvcnVuL3NlY3JldHMva3ViZXJuZXRlcy5pby9zZXJ2aWNlYWNjb3Vud"
                                 "C90b2tlbhILeC10b2tlbi1iaW4="));
  auto secret_provider1 = secret_manager->findOrCreateTlsCertificateProvider(
      config_source, "abc.com", secret_context, init_manager);

  // The base64 encoded proto binary is identical to the one above, but in different field order.
  // It is also identical to the YAML below.
  config_source.mutable_api_config_source()
      ->mutable_grpc_services(0)
      ->mutable_google_grpc()
      ->mutable_call_credentials(0)
      ->mutable_from_plugin()
      ->mutable_typed_config()
      ->set_value(Base64::decode("Egt4LXRva2VuLWJpbgo1CjMvdmFyL3J1bi9zZWNyZXRzL2t1YmVybmV0ZXMuaW8vc"
                                 "2VydmljZWFjY291bnQvdG9rZW4="));
  auto secret_provider2 = secret_manager->findOrCreateTlsCertificateProvider(
      config_source, "abc.com", secret_context, init_manager);

  API_NO_BOOST(envoy::config::grpc_credential::v2alpha::FileBasedMetadataConfig)
  file_based_metadata_config;
  TestUtility::loadFromYaml(R"(
header_key: x-token-bin
secret_data:
  filename: "/var/run/secrets/kubernetes.io/serviceaccount/token"
  )",
                            file_based_metadata_config);
  config_source.mutable_api_config_source()
      ->mutable_grpc_services(0)
      ->mutable_google_grpc()
      ->mutable_call_credentials(0)
      ->mutable_from_plugin()
      ->mutable_typed_config()
      ->PackFrom(file_based_metadata_config);
  auto secret_provider3 = secret_manager->findOrCreateTlsCertificateProvider(
      config_source, "abc.com", secret_context, init_manager);

  EXPECT_EQ(secret_provider1, secret_provider2);
  EXPECT_EQ(secret_provider2, secret_provider3);
}

TEST_F(SecretManagerImplTest, SdsDynamicSecretUpdateSuccess) {
  Server::MockInstance server;
  SecretManagerPtr secret_manager(new SecretManagerImpl(config_tracker_));

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> secret_context;

  envoy::config::core::v3::ConfigSource config_source;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Random::MockRandomGenerator> random;
  Stats::IsolatedStoreImpl stats;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Init::ExpectableWatcherImpl> init_watcher;
  Init::TargetHandlePtr init_target_handle;
  EXPECT_CALL(init_manager, add(_))
      .WillOnce(Invoke([&init_target_handle](const Init::Target& target) {
        init_target_handle = target.createHandle("test");
      }));
  EXPECT_CALL(secret_context, stats()).WillOnce(ReturnRef(stats));
  EXPECT_CALL(secret_context, initManager()).Times(0);
  EXPECT_CALL(secret_context, mainThreadDispatcher()).WillRepeatedly(ReturnRef(*dispatcher_));
  EXPECT_CALL(secret_context, localInfo()).WillOnce(ReturnRef(local_info));
  EXPECT_CALL(secret_context, api()).WillRepeatedly(ReturnRef(*api_));

  auto secret_provider = secret_manager->findOrCreateTlsCertificateProvider(
      config_source, "abc.com", secret_context, init_manager);
  const std::string yaml =
      R"EOF(
name: "abc.com"
tls_certificate:
  certificate_chain:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem"
)EOF";
  envoy::extensions::transport_sockets::tls::v3::Secret typed_secret;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), typed_secret);
  const auto decoded_resources = TestUtility::decodeResources({typed_secret});
  init_target_handle->initialize(init_watcher);
  secret_context.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, "");
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> ctx;
  Ssl::TlsCertificateConfigImpl tls_config(*secret_provider->secret(), ctx, *api_);
  const std::string cert_pem =
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(cert_pem)),
            tls_config.certificateChain());
  const std::string key_pem =
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_key.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(key_pem)),
            tls_config.privateKey());
}

TEST_F(SecretManagerImplTest, SdsDynamicGenericSecret) {
  Server::MockInstance server;
  SecretManagerPtr secret_manager(new SecretManagerImpl(config_tracker_));
  envoy::config::core::v3::ConfigSource config_source;

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> secret_context;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;
  Stats::IsolatedStoreImpl stats;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  Init::TargetHandlePtr init_target_handle;
  NiceMock<Init::ExpectableWatcherImpl> init_watcher;

  EXPECT_CALL(secret_context, mainThreadDispatcher()).WillRepeatedly(ReturnRef(*dispatcher_));
  EXPECT_CALL(secret_context, messageValidationVisitor()).WillOnce(ReturnRef(validation_visitor));
  EXPECT_CALL(secret_context, stats()).WillOnce(ReturnRef(stats));
  EXPECT_CALL(secret_context, initManager()).Times(0);
  EXPECT_CALL(secret_context, localInfo()).WillOnce(ReturnRef(local_info));
  EXPECT_CALL(secret_context, api()).WillRepeatedly(ReturnRef(*api_));
  EXPECT_CALL(init_manager, add(_))
      .WillOnce(Invoke([&init_target_handle](const Init::Target& target) {
        init_target_handle = target.createHandle("test");
      }));

  auto secret_provider = secret_manager->findOrCreateGenericSecretProvider(
      config_source, "encryption_key", secret_context, init_manager);

  const std::string yaml = R"EOF(
name: "encryption_key"
generic_secret:
  secret:
    inline_string: "DUMMY_AES_128_KEY"
)EOF";
  envoy::extensions::transport_sockets::tls::v3::Secret typed_secret;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), typed_secret);
  const auto decoded_resources = TestUtility::decodeResources({typed_secret});
  init_target_handle->initialize(init_watcher);
  secret_context.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, "");

  const envoy::extensions::transport_sockets::tls::v3::GenericSecret generic_secret(
      *secret_provider->secret());
  EXPECT_EQ("DUMMY_AES_128_KEY", generic_secret.secret().inline_string());
}

TEST_F(SecretManagerImplTest, ConfigDumpHandler) {
  Server::MockInstance server;
  auto secret_manager = std::make_unique<SecretManagerImpl>(config_tracker_);
  time_system_.setSystemTime(std::chrono::milliseconds(1234567891234));

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> secret_context;

  envoy::config::core::v3::ConfigSource config_source;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Random::MockRandomGenerator> random;
  Stats::IsolatedStoreImpl stats;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Init::ExpectableWatcherImpl> init_watcher;
  Init::TargetHandlePtr init_target_handle;
  EXPECT_CALL(init_manager, add(_))
      .WillRepeatedly(Invoke([&init_target_handle](const Init::Target& target) {
        init_target_handle = target.createHandle("test");
      }));
  EXPECT_CALL(secret_context, stats()).WillRepeatedly(ReturnRef(stats));
  EXPECT_CALL(secret_context, initManager()).Times(0);
  EXPECT_CALL(secret_context, mainThreadDispatcher()).WillRepeatedly(ReturnRef(dispatcher));
  EXPECT_CALL(secret_context, localInfo()).WillRepeatedly(ReturnRef(local_info));

  auto secret_provider = secret_manager->findOrCreateTlsCertificateProvider(
      config_source, "abc.com", secret_context, init_manager);
  const std::string yaml =
      R"EOF(
name: "abc.com"
tls_certificate:
  certificate_chain:
    inline_string: "DUMMY_INLINE_BYTES_FOR_CERT_CHAIN"
  private_key:
    inline_string: "DUMMY_INLINE_BYTES_FOR_PRIVATE_KEY"
  password:
    inline_string: "DUMMY_PASSWORD"
)EOF";
  envoy::extensions::transport_sockets::tls::v3::Secret typed_secret;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), typed_secret);
  const auto decoded_resources = TestUtility::decodeResources({typed_secret});
  init_target_handle->initialize(init_watcher);
  secret_context.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, "keycert-v1");
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> ctx;
  Ssl::TlsCertificateConfigImpl tls_config(*secret_provider->secret(), ctx, *api_);
  EXPECT_EQ("DUMMY_INLINE_BYTES_FOR_CERT_CHAIN", tls_config.certificateChain());
  EXPECT_EQ("DUMMY_INLINE_BYTES_FOR_PRIVATE_KEY", tls_config.privateKey());
  EXPECT_EQ("DUMMY_PASSWORD", tls_config.password());

  // Private key and password are removed.
  const std::string expected_secrets_config_dump = R"EOF(
dynamic_active_secrets:
- name: "abc.com"
  version_info: "keycert-v1"
  last_updated:
    seconds: 1234567891
    nanos: 234000000
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com"
    tls_certificate:
      certificate_chain:
        inline_string: "DUMMY_INLINE_BYTES_FOR_CERT_CHAIN"
      private_key:
        inline_string: "[redacted]"
      password:
        inline_string: "[redacted]"
)EOF";
  checkConfigDump(expected_secrets_config_dump);
  StrictMock<MockStringMatcher> mock_matcher;
  EXPECT_CALL(mock_matcher, match("abc.com")).WillOnce(Return(false));
  checkConfigDump("{}", mock_matcher);

  // Add a dynamic tls validation context provider.
  time_system_.setSystemTime(std::chrono::milliseconds(1234567899000));
  auto context_secret_provider = secret_manager->findOrCreateCertificateValidationContextProvider(
      config_source, "abc.com.validation", secret_context, init_manager);
  const std::string validation_yaml = R"EOF(
name: "abc.com.validation"
validation_context:
  trusted_ca:
    inline_string: "DUMMY_INLINE_STRING_TRUSTED_CA"
)EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(validation_yaml), typed_secret);
  const auto decoded_resources_2 = TestUtility::decodeResources({typed_secret});

  init_target_handle->initialize(init_watcher);
  secret_context.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources_2.refvec_, "validation-context-v1");
  Ssl::CertificateValidationContextConfigImpl cert_validation_context(
      *context_secret_provider->secret(), *api_);
  EXPECT_EQ("DUMMY_INLINE_STRING_TRUSTED_CA", cert_validation_context.caCert());
  const std::string updated_config_dump = R"EOF(
dynamic_active_secrets:
- name: "abc.com"
  version_info: "keycert-v1"
  last_updated:
    seconds: 1234567891
    nanos: 234000000
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com"
    tls_certificate:
      certificate_chain:
        inline_string: "DUMMY_INLINE_BYTES_FOR_CERT_CHAIN"
      private_key:
        inline_string: "[redacted]"
      password:
        inline_string: "[redacted]"
- name: "abc.com.validation"
  version_info: "validation-context-v1"
  last_updated:
    seconds: 1234567899
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com.validation"
    validation_context:
      trusted_ca:
        inline_string: "DUMMY_INLINE_STRING_TRUSTED_CA"
)EOF";
  checkConfigDump(updated_config_dump);
  EXPECT_CALL(mock_matcher, match("abc.com")).WillOnce(Return(false));
  EXPECT_CALL(mock_matcher, match("abc.com.validation")).WillOnce(Return(false));
  checkConfigDump("{}", mock_matcher);

  // Add a dynamic tls session ticket encryption keys context provider.
  time_system_.setSystemTime(std::chrono::milliseconds(1234567899000));
  auto stek_secret_provider = secret_manager->findOrCreateTlsSessionTicketKeysContextProvider(
      config_source, "abc.com.stek", secret_context, init_manager);
  const std::string stek_yaml = R"EOF(
name: "abc.com.stek"
session_ticket_keys:
  keys:
    - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a"
    - inline_string: "DUMMY_INLINE_STRING"
    - inline_bytes: "RFVNTVlfSU5MSU5FX0JZVEVT"
)EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(stek_yaml), typed_secret);
  const auto decoded_resources_3 = TestUtility::decodeResources({typed_secret});

  init_target_handle->initialize(init_watcher);
  secret_context.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources_3.refvec_, "stek-context-v1");
  EXPECT_EQ(stek_secret_provider->secret()->keys()[1].inline_string(), "DUMMY_INLINE_STRING");

  const std::string updated_once_more_config_dump = R"EOF(
dynamic_active_secrets:
- name: "abc.com"
  version_info: "keycert-v1"
  last_updated:
    seconds: 1234567891
    nanos: 234000000
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com"
    tls_certificate:
      certificate_chain:
        inline_string: "DUMMY_INLINE_BYTES_FOR_CERT_CHAIN"
      private_key:
        inline_string: "[redacted]"
      password:
        inline_string: "[redacted]"
- name: "abc.com.validation"
  version_info: "validation-context-v1"
  last_updated:
    seconds: 1234567899
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com.validation"
    validation_context:
      trusted_ca:
        inline_string: "DUMMY_INLINE_STRING_TRUSTED_CA"
- name: "abc.com.stek"
  version_info: "stek-context-v1"
  last_updated:
    seconds: 1234567899
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com.stek"
    session_ticket_keys:
      keys:
        - filename: "[redacted]"
        - inline_string: "[redacted]"
        - inline_bytes: "W3JlZGFjdGVkXQ=="
)EOF";
  checkConfigDump(TestEnvironment::substitute(updated_once_more_config_dump));
  EXPECT_CALL(mock_matcher, match("abc.com")).WillOnce(Return(false));
  EXPECT_CALL(mock_matcher, match("abc.com.validation")).WillOnce(Return(false));
  EXPECT_CALL(mock_matcher, match("abc.com.stek")).WillOnce(Return(false));
  checkConfigDump("{}", mock_matcher);

  // Add a dynamic generic secret provider.
  time_system_.setSystemTime(std::chrono::milliseconds(1234567900000));
  auto generic_secret_provider = secret_manager->findOrCreateGenericSecretProvider(
      config_source, "signing_key", secret_context, init_manager);

  const std::string generic_secret_yaml = R"EOF(
name: "signing_key"
generic_secret:
  secret:
    inline_string: "DUMMY_ECDSA_KEY"
)EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(generic_secret_yaml), typed_secret);
  const auto decoded_resources_4 = TestUtility::decodeResources({typed_secret});
  init_target_handle->initialize(init_watcher);
  secret_context.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources_4.refvec_, "signing-key-v1");

  const envoy::extensions::transport_sockets::tls::v3::GenericSecret generic_secret(
      *generic_secret_provider->secret());
  EXPECT_EQ("DUMMY_ECDSA_KEY", generic_secret.secret().inline_string());

  const std::string config_dump_with_generic_secret = R"EOF(
dynamic_active_secrets:
- name: "abc.com"
  version_info: "keycert-v1"
  last_updated:
    seconds: 1234567891
    nanos: 234000000
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com"
    tls_certificate:
      certificate_chain:
        inline_string: "DUMMY_INLINE_BYTES_FOR_CERT_CHAIN"
      private_key:
        inline_string: "[redacted]"
      password:
        inline_string: "[redacted]"
- name: "abc.com.validation"
  version_info: "validation-context-v1"
  last_updated:
    seconds: 1234567899
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com.validation"
    validation_context:
      trusted_ca:
        inline_string: "DUMMY_INLINE_STRING_TRUSTED_CA"
- name: "abc.com.stek"
  version_info: "stek-context-v1"
  last_updated:
    seconds: 1234567899
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com.stek"
    session_ticket_keys:
      keys:
        - filename: "[redacted]"
        - inline_string: "[redacted]"
        - inline_bytes: "W3JlZGFjdGVkXQ=="
- name: "signing_key"
  version_info: "signing-key-v1"
  last_updated:
    seconds: 1234567900
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "signing_key"
    generic_secret:
      secret:
        inline_string: "[redacted]"
)EOF";
  checkConfigDump(TestEnvironment::substitute(config_dump_with_generic_secret));
  EXPECT_CALL(mock_matcher, match("abc.com")).WillOnce(Return(false));
  EXPECT_CALL(mock_matcher, match("abc.com.validation")).WillOnce(Return(false));
  EXPECT_CALL(mock_matcher, match("abc.com.stek")).WillOnce(Return(false));
  EXPECT_CALL(mock_matcher, match("signing_key")).WillOnce(Return(false));
  checkConfigDump("{}", mock_matcher);
}

TEST_F(SecretManagerImplTest, ConfigDumpHandlerWarmingSecrets) {
  Server::MockInstance server;
  auto secret_manager = std::make_unique<SecretManagerImpl>(config_tracker_);
  time_system_.setSystemTime(std::chrono::milliseconds(1234567891234));

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> secret_context;

  envoy::config::core::v3::ConfigSource config_source;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Random::MockRandomGenerator> random;
  Stats::IsolatedStoreImpl stats;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Init::ExpectableWatcherImpl> init_watcher;
  Init::TargetHandlePtr init_target_handle;
  EXPECT_CALL(init_manager, add(_))
      .WillRepeatedly(Invoke([&init_target_handle](const Init::Target& target) {
        init_target_handle = target.createHandle("test");
      }));
  EXPECT_CALL(secret_context, stats()).WillRepeatedly(ReturnRef(stats));
  EXPECT_CALL(secret_context, initManager()).Times(0);
  EXPECT_CALL(secret_context, mainThreadDispatcher()).WillRepeatedly(ReturnRef(dispatcher));
  EXPECT_CALL(secret_context, localInfo()).WillRepeatedly(ReturnRef(local_info));

  auto secret_provider = secret_manager->findOrCreateTlsCertificateProvider(
      config_source, "abc.com", secret_context, init_manager);
  const std::string expected_secrets_config_dump = R"EOF(
dynamic_warming_secrets:
- name: "abc.com"
  version_info: "uninitialized"
  last_updated:
    seconds: 1234567891
    nanos: 234000000
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com"
  )EOF";
  checkConfigDump(expected_secrets_config_dump);
  StrictMock<MockStringMatcher> mock_matcher;
  EXPECT_CALL(mock_matcher, match("abc.com")).WillOnce(Return(false));
  checkConfigDump("{}", mock_matcher);

  time_system_.setSystemTime(std::chrono::milliseconds(1234567899000));
  auto context_secret_provider = secret_manager->findOrCreateCertificateValidationContextProvider(
      config_source, "abc.com.validation", secret_context, init_manager);
  init_target_handle->initialize(init_watcher);
  const std::string updated_config_dump = R"EOF(
dynamic_warming_secrets:
- name: "abc.com"
  version_info: "uninitialized"
  last_updated:
    seconds: 1234567891
    nanos: 234000000
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com"
- name: "abc.com.validation"
  version_info: "uninitialized"
  last_updated:
    seconds: 1234567899
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com.validation"
)EOF";
  checkConfigDump(updated_config_dump);
  EXPECT_CALL(mock_matcher, match("abc.com")).WillOnce(Return(false));
  EXPECT_CALL(mock_matcher, match("abc.com.validation")).WillOnce(Return(false));
  checkConfigDump("{}", mock_matcher);

  time_system_.setSystemTime(std::chrono::milliseconds(1234567899000));
  auto stek_secret_provider = secret_manager->findOrCreateTlsSessionTicketKeysContextProvider(
      config_source, "abc.com.stek", secret_context, init_manager);
  init_target_handle->initialize(init_watcher);
  const std::string updated_once_more_config_dump = R"EOF(
dynamic_warming_secrets:
- name: "abc.com"
  version_info: "uninitialized"
  last_updated:
    seconds: 1234567891
    nanos: 234000000
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com"
- name: "abc.com.validation"
  version_info: "uninitialized"
  last_updated:
    seconds: 1234567899
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com.validation"
- name: "abc.com.stek"
  version_info: "uninitialized"
  last_updated:
    seconds: 1234567899
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com.stek"
)EOF";
  checkConfigDump(updated_once_more_config_dump);
  EXPECT_CALL(mock_matcher, match("abc.com")).WillOnce(Return(false));
  EXPECT_CALL(mock_matcher, match("abc.com.validation")).WillOnce(Return(false));
  EXPECT_CALL(mock_matcher, match("abc.com.stek")).WillOnce(Return(false));
  checkConfigDump("{}", mock_matcher);

  time_system_.setSystemTime(std::chrono::milliseconds(1234567900000));
  auto generic_secret_provider = secret_manager->findOrCreateGenericSecretProvider(
      config_source, "signing_key", secret_context, init_manager);
  init_target_handle->initialize(init_watcher);
  const std::string config_dump_with_generic_secret = R"EOF(
dynamic_warming_secrets:
- name: "abc.com"
  version_info: "uninitialized"
  last_updated:
    seconds: 1234567891
    nanos: 234000000
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com"
- name: "abc.com.validation"
  version_info: "uninitialized"
  last_updated:
    seconds: 1234567899
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com.validation"
- name: "abc.com.stek"
  version_info: "uninitialized"
  last_updated:
    seconds: 1234567899
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com.stek"
- name: "signing_key"
  version_info: "uninitialized"
  last_updated:
    seconds: 1234567900
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "signing_key"
)EOF";
  checkConfigDump(config_dump_with_generic_secret);
  EXPECT_CALL(mock_matcher, match("abc.com")).WillOnce(Return(false));
  EXPECT_CALL(mock_matcher, match("abc.com.validation")).WillOnce(Return(false));
  EXPECT_CALL(mock_matcher, match("abc.com.stek")).WillOnce(Return(false));
  EXPECT_CALL(mock_matcher, match("signing_key")).WillOnce(Return(false));
  checkConfigDump("{}", mock_matcher);
}

TEST_F(SecretManagerImplTest, ConfigDumpHandlerStaticSecrets) {
  Server::MockInstance server;
  auto secret_manager = std::make_unique<SecretManagerImpl>(config_tracker_);
  time_system_.setSystemTime(std::chrono::milliseconds(1234567891234));

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> secret_context;

  envoy::config::core::v3::ConfigSource config_source;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Random::MockRandomGenerator> random;
  Stats::IsolatedStoreImpl stats;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Init::ExpectableWatcherImpl> init_watcher;
  Init::TargetHandlePtr init_target_handle;
  EXPECT_CALL(init_manager, add(_))
      .WillRepeatedly(Invoke([&init_target_handle](const Init::Target& target) {
        init_target_handle = target.createHandle("test");
      }));
  EXPECT_CALL(secret_context, stats()).WillRepeatedly(ReturnRef(stats));
  EXPECT_CALL(secret_context, initManager()).Times(0);
  EXPECT_CALL(secret_context, mainThreadDispatcher()).WillRepeatedly(ReturnRef(dispatcher));
  EXPECT_CALL(secret_context, localInfo()).WillRepeatedly(ReturnRef(local_info));

  const std::string tls_certificate =
      R"EOF(
name: "abc.com"
tls_certificate:
  certificate_chain:
    inline_string: "DUMMY_INLINE_BYTES_FOR_CERT_CHAIN"
  private_key:
    inline_string: "DUMMY_INLINE_BYTES_FOR_PRIVATE_KEY"
  password:
    inline_string: "DUMMY_PASSWORD"
)EOF";
  envoy::extensions::transport_sockets::tls::v3::Secret tls_cert_secret;
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate), tls_cert_secret);
  secret_manager->addStaticSecret(tls_cert_secret);
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: "abc.com.nopassword"
tls_certificate:
  certificate_chain:
    inline_string: "DUMMY_INLINE_BYTES_FOR_CERT_CHAIN"
  private_key:
    inline_string: "DUMMY_INLINE_BYTES_FOR_PRIVATE_KEY"
)EOF"),
                            tls_cert_secret);
  secret_manager->addStaticSecret(tls_cert_secret);
  const std::string expected_config_dump = R"EOF(
static_secrets:
- name: "abc.com.nopassword"
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com.nopassword"
    tls_certificate:
      certificate_chain:
        inline_string: "DUMMY_INLINE_BYTES_FOR_CERT_CHAIN"
      private_key:
        inline_string: "[redacted]"
- name: "abc.com"
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com"
    tls_certificate:
      certificate_chain:
        inline_string: "DUMMY_INLINE_BYTES_FOR_CERT_CHAIN"
      private_key:
        inline_string: "[redacted]"
      password:
        inline_string: "[redacted]"
)EOF";
  checkConfigDump(expected_config_dump);
  StrictMock<MockStringMatcher> mock_matcher;
  EXPECT_CALL(mock_matcher, match(testing::HasSubstr("abc.com")))
      .Times(2)
      .WillRepeatedly(Return(false));
  checkConfigDump("{}", mock_matcher);
}

TEST_F(SecretManagerImplTest, ConfigDumpHandlerStaticValidationContext) {
  Server::MockInstance server;
  auto secret_manager = std::make_unique<SecretManagerImpl>(config_tracker_);
  time_system_.setSystemTime(std::chrono::milliseconds(1234567891234));
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> secret_context;
  envoy::config::core::v3::ConfigSource config_source;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Random::MockRandomGenerator> random;
  Stats::IsolatedStoreImpl stats;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Init::ExpectableWatcherImpl> init_watcher;
  Init::TargetHandlePtr init_target_handle;
  EXPECT_CALL(init_manager, add(_))
      .WillRepeatedly(Invoke([&init_target_handle](const Init::Target& target) {
        init_target_handle = target.createHandle("test");
      }));
  EXPECT_CALL(secret_context, stats()).WillRepeatedly(ReturnRef(stats));
  EXPECT_CALL(secret_context, initManager()).Times(0);
  EXPECT_CALL(secret_context, mainThreadDispatcher()).WillRepeatedly(ReturnRef(dispatcher));
  EXPECT_CALL(secret_context, localInfo()).WillRepeatedly(ReturnRef(local_info));

  const std::string validation_context =
      R"EOF(
name: "abc.com.validation"
validation_context:
  trusted_ca:
    inline_string: "DUMMY_INLINE_STRING_TRUSTED_CA"
)EOF";
  envoy::extensions::transport_sockets::tls::v3::Secret validation_secret;
  TestUtility::loadFromYaml(TestEnvironment::substitute(validation_context), validation_secret);
  secret_manager->addStaticSecret(validation_secret);
  const std::string expected_config_dump = R"EOF(
static_secrets:
- name: "abc.com.validation"
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com.validation"
    validation_context:
      trusted_ca:
        inline_string: "DUMMY_INLINE_STRING_TRUSTED_CA"
)EOF";
  checkConfigDump(expected_config_dump);
  StrictMock<MockStringMatcher> mock_matcher;
  EXPECT_CALL(mock_matcher, match("abc.com.validation")).WillOnce(Return(false));
  checkConfigDump("{}", mock_matcher);
}

TEST_F(SecretManagerImplTest, ConfigDumpHandlerStaticSessionTicketsContext) {
  Server::MockInstance server;
  auto secret_manager = std::make_unique<SecretManagerImpl>(config_tracker_);
  time_system_.setSystemTime(std::chrono::milliseconds(1234567891234));
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> secret_context;
  envoy::config::core::v3::ConfigSource config_source;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Random::MockRandomGenerator> random;
  Stats::IsolatedStoreImpl stats;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Init::ExpectableWatcherImpl> init_watcher;
  Init::TargetHandlePtr init_target_handle;
  EXPECT_CALL(init_manager, add(_))
      .WillRepeatedly(Invoke([&init_target_handle](const Init::Target& target) {
        init_target_handle = target.createHandle("test");
      }));
  EXPECT_CALL(secret_context, stats()).WillRepeatedly(ReturnRef(stats));
  EXPECT_CALL(secret_context, initManager()).Times(0);
  EXPECT_CALL(secret_context, mainThreadDispatcher()).WillRepeatedly(ReturnRef(dispatcher));
  EXPECT_CALL(secret_context, localInfo()).WillRepeatedly(ReturnRef(local_info));

  const std::string stek_context =
      R"EOF(
name: "abc.com.stek"
session_ticket_keys:
  keys:
    - filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ticket_key_a"
    - inline_string: "DUMMY_INLINE_STRING"
    - inline_bytes: "RFVNTVlfSU5MSU5FX0JZVEVT"
)EOF";
  envoy::extensions::transport_sockets::tls::v3::Secret stek_secret;
  TestUtility::loadFromYaml(TestEnvironment::substitute(stek_context), stek_secret);
  secret_manager->addStaticSecret(stek_secret);
  const std::string expected_config_dump = R"EOF(
static_secrets:
- name: "abc.com.stek"
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "abc.com.stek"
    session_ticket_keys:
      keys:
        - filename: "[redacted]"
        - inline_string: "[redacted]"
        - inline_bytes: "W3JlZGFjdGVkXQ=="
)EOF";
  checkConfigDump(TestEnvironment::substitute(expected_config_dump));
  StrictMock<MockStringMatcher> mock_matcher;
  EXPECT_CALL(mock_matcher, match("abc.com.stek")).WillOnce(Return(false));
  checkConfigDump("{}", mock_matcher);
}

TEST_F(SecretManagerImplTest, ConfigDumpHandlerStaticGenericSecret) {
  auto secret_manager = std::make_unique<SecretManagerImpl>(config_tracker_);

  const std::string yaml = R"EOF(
name: "signing_key"
generic_secret:
  secret:
    inline_bytes: "DUMMY_ECDSA_KEY"
)EOF";
  envoy::extensions::transport_sockets::tls::v3::Secret typed_secret;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), typed_secret);
  secret_manager->addStaticSecret(typed_secret);

  const std::string expected_config_dump = R"EOF(
static_secrets:
- name: "signing_key"
  secret:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: "signing_key"
    generic_secret:
      secret:
        inline_bytes: "W3JlZGFjdGVkXQ=="
)EOF";
  checkConfigDump(TestEnvironment::substitute(expected_config_dump));
  StrictMock<MockStringMatcher> mock_matcher;
  EXPECT_CALL(mock_matcher, match("signing_key")).WillOnce(Return(false));
  checkConfigDump("{}", mock_matcher);
}

// Test that private key provider definitions inside Secrets can be added dynamically.
TEST_F(SecretManagerImplTest, SdsDynamicSecretPrivateKeyProviderUpdateSuccess) {
  Server::MockInstance server;
  SecretManagerPtr secret_manager(new SecretManagerImpl(config_tracker_));
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> secret_context;

  envoy::config::core::v3::ConfigSource config_source;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Random::MockRandomGenerator> random;
  Stats::IsolatedStoreImpl stats;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Init::ExpectableWatcherImpl> init_watcher;
  Init::TargetHandlePtr init_target_handle;
  EXPECT_CALL(init_manager, add(_))
      .WillOnce(Invoke([&init_target_handle](const Init::Target& target) {
        init_target_handle = target.createHandle("test");
      }));
  EXPECT_CALL(secret_context, stats()).WillOnce(ReturnRef(stats));
  EXPECT_CALL(secret_context, initManager()).Times(0);
  EXPECT_CALL(secret_context, mainThreadDispatcher()).WillRepeatedly(ReturnRef(*dispatcher_));
  EXPECT_CALL(secret_context, localInfo()).WillOnce(ReturnRef(local_info));
  EXPECT_CALL(secret_context, api()).WillRepeatedly(ReturnRef(*api_));

  auto secret_provider = secret_manager->findOrCreateTlsCertificateProvider(
      config_source, "abc.com", secret_context, init_manager);
  const std::string yaml =
      R"EOF(
name: "abc.com"
tls_certificate:
  certificate_chain:
    filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/selfsigned_cert.pem"
  private_key_provider:
    provider_name: test
    typed_config:
      "@type": type.googleapis.com/test.common.secret.TestPrivateKeyMethodConfig
)EOF";
  envoy::extensions::transport_sockets::tls::v3::Secret typed_secret;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), typed_secret);
  EXPECT_TRUE(typed_secret.tls_certificate().has_private_key_provider());
  EXPECT_FALSE(typed_secret.tls_certificate().has_private_key());
  const auto decoded_resources = TestUtility::decodeResources({typed_secret});
  init_target_handle->initialize(init_watcher);
  secret_context.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, "");
  EXPECT_TRUE(secret_provider->secret()->has_private_key_provider());
  EXPECT_FALSE(secret_provider->secret()->has_private_key());

  // Fail because there isn't a real private key message provider, but not because the configuration
  // is incorrect.
  testing::NiceMock<Ssl::MockPrivateKeyMethodManager> private_key_method_manager;
  testing::NiceMock<Ssl::MockContextManager> ssl_context_manager;
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> ctx;
  EXPECT_CALL(private_key_method_manager, createPrivateKeyMethodProvider(_, _))
      .WillRepeatedly(Return(nullptr));
  EXPECT_CALL(ssl_context_manager, privateKeyMethodManager())
      .WillRepeatedly(ReturnRef(private_key_method_manager));
  EXPECT_CALL(ctx, sslContextManager()).WillRepeatedly(ReturnRef(ssl_context_manager));
  EXPECT_THROW_WITH_MESSAGE(
      Ssl::TlsCertificateConfigImpl tls_config(*secret_provider->secret(), ctx, *api_),
      EnvoyException, "Failed to load private key provider: test");
}

// Verify that using the match_subject_alt_names will result in a typed matcher, one for each of
// DNS, URI, EMAIL and IP_ADDRESS.
// TODO(pradeepcrao): Delete this test once the deprecated field is removed.
TEST_F(SecretManagerImplTest, DeprecatedSanMatcher) {
  envoy::extensions::transport_sockets::tls::v3::Secret secret_config;
  const std::string yaml =
      R"EOF(
      name: "abc.com"
      validation_context:
        trusted_ca: { filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem" }
        allow_expired_certificate: true
        match_subject_alt_names:
          exact: "example.foo"
      )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);
  std::unique_ptr<SecretManager> secret_manager(new SecretManagerImpl(config_tracker_));
  secret_manager->addStaticSecret(secret_config);

  ASSERT_EQ(secret_manager->findStaticCertificateValidationContextProvider("undefined"), nullptr);
  ASSERT_NE(secret_manager->findStaticCertificateValidationContextProvider("abc.com"), nullptr);
  Ssl::CertificateValidationContextConfigImpl cvc_config(
      *secret_manager->findStaticCertificateValidationContextProvider("abc.com")->secret(), *api_);
  EXPECT_EQ(cvc_config.subjectAltNameMatchers().size(), 4);
  EXPECT_EQ("example.foo", cvc_config.subjectAltNameMatchers()[0].matcher().exact());
  EXPECT_EQ(envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::DNS,
            cvc_config.subjectAltNameMatchers()[0].san_type());
  EXPECT_EQ("example.foo", cvc_config.subjectAltNameMatchers()[1].matcher().exact());
  EXPECT_EQ(envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::URI,
            cvc_config.subjectAltNameMatchers()[1].san_type());
  EXPECT_EQ("example.foo", cvc_config.subjectAltNameMatchers()[2].matcher().exact());
  EXPECT_EQ(envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::EMAIL,
            cvc_config.subjectAltNameMatchers()[2].san_type());
  EXPECT_EQ("example.foo", cvc_config.subjectAltNameMatchers()[3].matcher().exact());
  EXPECT_EQ(envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::IP_ADDRESS,
            cvc_config.subjectAltNameMatchers()[3].san_type());
}

} // namespace
} // namespace Secret
} // namespace Envoy
