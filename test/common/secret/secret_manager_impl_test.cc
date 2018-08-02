#include <memory>

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/common/exception.h"

#include "common/secret/sds_api.h"
#include "common/secret/secret_manager_impl.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Secret {
namespace {

class MockServer : public Server::MockInstance {
public:
  Init::Manager& initManager() { return initmanager_; }

private:
  class InitManager : public Init::Manager {
  public:
    void registerTarget(Init::Target&) override {}
  };

  InitManager initmanager_;
};

class SecretManagerImplTest : public testing::Test {};

TEST_F(SecretManagerImplTest, SecretLoadSuccess) {
  envoy::api::v2::auth::Secret secret_config;

  std::string yaml =
      R"EOF(
name: "abc.com"
tls_certificate:
  certificate_chain:
    filename: "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_key.pem"
)EOF";

  MessageUtil::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);

  Server::MockInstance server;

  server.secretManager().addStaticSecret(secret_config);

  ASSERT_EQ(server.secretManager().findStaticTlsCertificate("undefined"), nullptr);

  ASSERT_NE(server.secretManager().findStaticTlsCertificate("abc.com"), nullptr);

  const std::string cert_pem = "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_cert.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(cert_pem)),
            server.secretManager().findStaticTlsCertificate("abc.com")->certificateChain());

  const std::string key_pem = "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_key.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(key_pem)),
            server.secretManager().findStaticTlsCertificate("abc.com")->privateKey());
}

TEST_F(SecretManagerImplTest, SdsDynamicSecretUpdateSuccess) {
  MockServer server;
  NiceMock<Init::MockManager> init_manager;
  envoy::api::v2::core::ConfigSource config_source;
  {
    auto secret_provider = server.secretManager().findOrCreateDynamicTlsCertificateSecretProvider(
        config_source, "abc.com", init_manager);

    std::string yaml =
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
    std::dynamic_pointer_cast<SdsApi>(secret_provider)->onConfigUpdate(secret_resources, "");

    const std::string cert_pem = "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_cert.pem";
    EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(cert_pem)),
              secret_provider->secret()->certificateChain());

    const std::string key_pem = "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_key.pem";
    EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(key_pem)),
              secret_provider->secret()->privateKey());
  }
}

TEST_F(SecretManagerImplTest, NotImplementedException) {
  envoy::api::v2::core::ConfigSource config_source;
  envoy::api::v2::auth::Secret secret_config;

  std::string yaml =
      R"EOF(
name: "abc.com"
session_ticket_keys:
  keys:
    - filename: "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_cert.pem"
)EOF";

  MessageUtil::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);

  MockServer server;
  std::unique_ptr<SecretManager> secret_manager(new SecretManagerImpl(server));

  EXPECT_THROW_WITH_MESSAGE(server.secretManager().addStaticSecret(secret_config), EnvoyException,
                            "Secret type not implemented");
}

} // namespace
} // namespace Secret
} // namespace Envoy