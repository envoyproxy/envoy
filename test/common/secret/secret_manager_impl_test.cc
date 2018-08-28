#include <memory>

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/common/exception.h"

#include "common/secret/secret_manager_impl.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

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

  const std::string cert_pem = "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_cert.pem";
  EXPECT_EQ(
      TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(cert_pem)),
      secret_manager->findStaticTlsCertificateProvider("abc.com")->secret()->certificateChain());

  const std::string key_pem = "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_key.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(key_pem)),
            secret_manager->findStaticTlsCertificateProvider("abc.com")->secret()->privateKey());
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
  const std::string cert_pem = "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(cert_pem)),
            secret_manager->findStaticCertificateValidationContextProvider("abc.com")
                ->secret()
                ->caCert());
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

} // namespace
} // namespace Secret
} // namespace Envoy
