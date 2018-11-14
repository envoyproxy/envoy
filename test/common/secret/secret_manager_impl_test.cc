#include <memory>

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/common/exception.h"

#include "common/secret/sds_api.h"
#include "common/secret/secret_manager_impl.h"
#include "common/ssl/certificate_validation_context_config_impl.h"
#include "common/ssl/tls_certificate_config_impl.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Secret {
namespace {

class SecretManagerImplTest : public testing::Test {};

// Validate that secret manager adds static TLS certificate secret successfully.
TEST_F(SecretManagerImplTest, TlsCertificateSecretLoadSuccess) {
  envoy::api::v2::auth::Secret secret_config;
  const std::string yaml =
      R"EOF(
name: "abc.com"
tls_certificate:
  certificate_chain:
    filename: "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_key.pem"
)EOF";
  MessageUtil::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);
  std::unique_ptr<SecretManager> secret_manager(new SecretManagerImpl());
  secret_manager->addStaticSecret(secret_config);

  ASSERT_EQ(secret_manager->findStaticTlsCertificateProvider("undefined"), nullptr);
  ASSERT_NE(secret_manager->findStaticTlsCertificateProvider("abc.com"), nullptr);

  Ssl::TlsCertificateConfigImpl tls_config(
      *secret_manager->findStaticTlsCertificateProvider("abc.com")->secret());
  const std::string cert_pem = "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_cert.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(cert_pem)),
            tls_config.certificateChain());

  const std::string key_pem = "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_key.pem";
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
        filename: "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_key.pem"
    )EOF";
  MessageUtil::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);
  std::unique_ptr<SecretManager> secret_manager(new SecretManagerImpl());
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
        trusted_ca: { filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem" }
        allow_expired_certificate: true
      )EOF";
  MessageUtil::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);
  std::unique_ptr<SecretManager> secret_manager(new SecretManagerImpl());
  secret_manager->addStaticSecret(secret_config);

  ASSERT_EQ(secret_manager->findStaticCertificateValidationContextProvider("undefined"), nullptr);
  ASSERT_NE(secret_manager->findStaticCertificateValidationContextProvider("abc.com"), nullptr);
  Ssl::CertificateValidationContextConfigImpl cvc_config(
      *secret_manager->findStaticCertificateValidationContextProvider("abc.com")->secret());
  const std::string cert_pem = "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem";
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
      trusted_ca: { filename: "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem" }
      allow_expired_certificate: true
    )EOF";
  MessageUtil::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);
  std::unique_ptr<SecretManager> secret_manager(new SecretManagerImpl());
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
    - filename: "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_cert.pem"
)EOF";

  MessageUtil::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);

  std::unique_ptr<SecretManager> secret_manager(new SecretManagerImpl());

  EXPECT_THROW_WITH_MESSAGE(secret_manager->addStaticSecret(secret_config), EnvoyException,
                            "Secret type not implemented");
}

TEST_F(SecretManagerImplTest, SdsDynamicSecretUpdateSuccess) {
  Server::MockInstance server;
  std::unique_ptr<SecretManager> secret_manager(std::make_unique<SecretManagerImpl>());

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> secret_context;

  envoy::api::v2::core::ConfigSource config_source;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockRandomGenerator> random;
  Stats::IsolatedStoreImpl stats;
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  NiceMock<Init::MockManager> init_manager;
  EXPECT_CALL(secret_context, localInfo()).WillOnce(ReturnRef(local_info));
  EXPECT_CALL(secret_context, dispatcher()).WillOnce(ReturnRef(dispatcher));
  EXPECT_CALL(secret_context, random()).WillOnce(ReturnRef(random));
  EXPECT_CALL(secret_context, stats()).WillOnce(ReturnRef(stats));
  EXPECT_CALL(secret_context, clusterManager()).WillOnce(ReturnRef(cluster_manager));
  EXPECT_CALL(secret_context, initManager()).WillRepeatedly(Return(&init_manager));

  auto secret_provider =
      secret_manager->findOrCreateTlsCertificateProvider(config_source, "abc.com", secret_context);
  const std::string yaml =
      R"EOF(
name: "abc.com"
tls_certificate:
  certificate_chain:
    filename: "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_key.pem"
)EOF";
  Protobuf::RepeatedPtrField<envoy::api::v2::auth::Secret> secret_resources;
  auto secret_config = secret_resources.Add();
  MessageUtil::loadFromYaml(TestEnvironment::substitute(yaml), *secret_config);
  dynamic_cast<TlsCertificateSdsApi&>(*secret_provider).onConfigUpdate(secret_resources, "");
  Ssl::TlsCertificateConfigImpl tls_config(*secret_provider->secret());
  const std::string cert_pem = "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_cert.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(cert_pem)),
            tls_config.certificateChain());
  const std::string key_pem = "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_key.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(key_pem)),
            tls_config.privateKey());
}

} // namespace
} // namespace Secret
} // namespace Envoy
