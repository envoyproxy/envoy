#include <memory>

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/common/exception.h"

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
    void initialize(std::function<void()> callback);
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
    filename: "test/common/ssl/test_data/selfsigned_cert.pem"
  private_key:
    filename: "test/common/ssl/test_data/selfsigned_key.pem"
)EOF";

  MessageUtil::loadFromYaml(yaml, secret_config);

  Server::MockInstance server;

  server.secretManager().addOrUpdateSecret("", secret_config);

  ASSERT_EQ(server.secretManager().findTlsCertificate("", "undefined"), nullptr);

  ASSERT_NE(server.secretManager().findTlsCertificate("", "abc.com"), nullptr);

  EXPECT_EQ(
      TestEnvironment::readFileToStringForTest("test/common/ssl/test_data/selfsigned_cert.pem"),
      server.secretManager().findTlsCertificate("", "abc.com")->certificateChain());

  EXPECT_EQ(
      TestEnvironment::readFileToStringForTest("test/common/ssl/test_data/selfsigned_key.pem"),
      server.secretManager().findTlsCertificate("", "abc.com")->privateKey());
}

TEST_F(SecretManagerImplTest, SdsDynamicSecretUpdateSuccess) {
  envoy::api::v2::core::ConfigSource config_source;
  envoy::api::v2::auth::Secret secret_config;

  std::string yaml =
      R"EOF(
  name: "abc.com"
  tls_certificate:
    certificate_chain:
      filename: "test/common/ssl/test_data/selfsigned_cert.pem"
    private_key:
      filename: "test/common/ssl/test_data/selfsigned_key.pem"
  )EOF";

  MessageUtil::loadFromYaml(yaml, secret_config);

  MockServer server;

  std::string config_source_hash =
      server.secretManager().addOrUpdateSdsService(config_source, "abc_config");

  server.secretManager().addOrUpdateSecret(config_source_hash, secret_config);

  ASSERT_EQ(server.secretManager().findTlsCertificate(config_source_hash, "undefined"), nullptr);

  EXPECT_EQ(
      TestEnvironment::readFileToStringForTest("test/common/ssl/test_data/selfsigned_cert.pem"),
      server.secretManager().findTlsCertificate(config_source_hash, "abc.com")->certificateChain());

  EXPECT_EQ(
      TestEnvironment::readFileToStringForTest("test/common/ssl/test_data/selfsigned_key.pem"),
      server.secretManager().findTlsCertificate(config_source_hash, "abc.com")->privateKey());
}

TEST_F(SecretManagerImplTest, NotImplementedException) {
  envoy::api::v2::core::ConfigSource config_source;
  envoy::api::v2::auth::Secret secret_config;

  std::string yaml =
      R"EOF(
name: "abc.com"
session_ticket_keys:
  keys:
    - filename: "test/common/ssl/test_data/selfsigned_cert.pem"
)EOF";

  MessageUtil::loadFromYaml(yaml, secret_config);

  MockServer server;
  std::unique_ptr<SecretManager> secret_manager(new SecretManagerImpl(server));

  std::string config_source_hash =
      server.secretManager().addOrUpdateSdsService(config_source, "abc_config");
  EXPECT_THROW_WITH_MESSAGE(
      server.secretManager().addOrUpdateSecret(config_source_hash, secret_config), EnvoyException,
      "Secret type not implemented");
}

} // namespace
} // namespace Secret
} // namespace Envoy
