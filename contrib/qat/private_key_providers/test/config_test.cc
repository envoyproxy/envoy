#include <string>

#include "source/common/common/random_generator.h"
#include "source/extensions/transport_sockets/tls/private_key/private_key_manager_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/common.h"
#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/registry.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "contrib/qat/private_key_providers/source/qat_private_key_provider.h"
#include "fake_factory.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Qat {

envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider
parsePrivateKeyProviderFromV3Yaml(const std::string& yaml_string) {
  envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider private_key_provider;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml_string), private_key_provider);
  return private_key_provider;
}

class FakeSingletonManager : public Singleton::Manager {
public:
  FakeSingletonManager(LibQatCryptoSharedPtr libqat) : libqat_(libqat) {}
  Singleton::InstanceSharedPtr get(const std::string&, Singleton::SingletonFactoryCb) override {
    return std::make_shared<QatManager>(libqat_);
  }

private:
  LibQatCryptoSharedPtr libqat_;
};

class QatConfigTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  QatConfigTest()
      : api_(Api::createApiForTest(store_, time_system_)),
        libqat_(std::make_shared<FakeLibQatCryptoImpl>()), fsm_(libqat_) {
    ON_CALL(factory_context_, api()).WillByDefault(ReturnRef(*api_));
    ON_CALL(factory_context_, sslContextManager()).WillByDefault(ReturnRef(context_manager_));
    ON_CALL(context_manager_, privateKeyMethodManager())
        .WillByDefault(ReturnRef(private_key_method_manager_));
    ON_CALL(factory_context_, singletonManager()).WillByDefault(ReturnRef(fsm_));
  }

  Ssl::PrivateKeyMethodProviderSharedPtr createWithConfig(std::string yaml) {
    FakeQatPrivateKeyMethodFactory qat_factory;
    Registry::InjectFactory<Ssl::PrivateKeyMethodProviderInstanceFactory>
        qat_private_key_method_factory(qat_factory);

    return factory_context_.sslContextManager()
        .privateKeyMethodManager()
        .createPrivateKeyMethodProvider(parsePrivateKeyProviderFromV3Yaml(yaml), factory_context_);
  }

  Event::SimulatedTimeSystem time_system_;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
  Stats::IsolatedStoreImpl store_;
  Api::ApiPtr api_;
  NiceMock<Ssl::MockContextManager> context_manager_;
  TransportSockets::Tls::PrivateKeyMethodManagerImpl private_key_method_manager_;
  std::shared_ptr<FakeLibQatCryptoImpl> libqat_;
  FakeSingletonManager fsm_;
};

TEST_F(QatConfigTest, CreateRsa1024) {
  const std::string yaml = R"EOF(
      provider_name: qat
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.qat.v3alpha.QatPrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key: { "filename": "{{ test_rundir }}/contrib/qat/private_key_providers/test/test_data/rsa-1024.pem" }
)EOF";

  Ssl::PrivateKeyMethodProviderSharedPtr provider = createWithConfig(yaml);
  EXPECT_NE(nullptr, provider);
  EXPECT_EQ(false, provider->checkFips());
  Ssl::BoringSslPrivateKeyMethodSharedPtr method = provider->getBoringSslPrivateKeyMethod();
  EXPECT_NE(nullptr, method);
}

TEST_F(QatConfigTest, CreateRsa2048) {
  const std::string yaml = R"EOF(
      provider_name: qat
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.qat.v3alpha.QatPrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key: { "filename": "{{ test_rundir }}/contrib/qat/private_key_providers/test/test_data/rsa-2048.pem" }
)EOF";

  EXPECT_NE(nullptr, createWithConfig(yaml));
}

TEST_F(QatConfigTest, CreateRsa3072) {
  const std::string yaml = R"EOF(
      provider_name: qat
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.qat.v3alpha.QatPrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key: { "filename": "{{ test_rundir }}/contrib/qat/private_key_providers/test/test_data/rsa-3072.pem" }
)EOF";

  EXPECT_NE(nullptr, createWithConfig(yaml));
}

TEST_F(QatConfigTest, CreateRsa4096) {
  const std::string yaml = R"EOF(
      provider_name: qat
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.qat.v3alpha.QatPrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key: { "filename": "{{ test_rundir }}/contrib/qat/private_key_providers/test/test_data/rsa-4096.pem" }
)EOF";

  EXPECT_NE(nullptr, createWithConfig(yaml));
}

TEST_F(QatConfigTest, CreateEcdsaP256) {
  const std::string yaml = R"EOF(
      provider_name: qat
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.qat.v3alpha.QatPrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key: { "filename": "{{ test_rundir }}/contrib/qat/private_key_providers/test/test_data/ecdsa-p256.pem" }
)EOF";

  EXPECT_THROW_WITH_MESSAGE(createWithConfig(yaml), EnvoyException, "Only RSA keys are supported.");
}

TEST_F(QatConfigTest, CreateMissingPrivateKeyFile) {
  const std::string yaml = R"EOF(
      provider_name: qat
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.qat.v3alpha.QatPrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key: { "filename": "{{ test_rundir }}/contrib/qat/private_key_providers/test/test_data/missing.pem" }
)EOF";

  EXPECT_THROW(createWithConfig(yaml), EnvoyException);
}

TEST_F(QatConfigTest, CreateMissingKey) {
  const std::string yaml = R"EOF(
      provider_name: qat
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.qat.v3alpha.QatPrivateKeyMethodConfig
        poll_delay: 0.02s
        )EOF";

  EXPECT_THROW_WITH_MESSAGE(createWithConfig(yaml), EnvoyException,
                            "Unexpected DataSource::specifier_case(): 0");
}

TEST_F(QatConfigTest, CreateMissingPollDelay) {
  const std::string yaml = R"EOF(
      provider_name: qat
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.qat.v3alpha.QatPrivateKeyMethodConfig
        private_key: { "filename": "{{ test_rundir }}/contrib/qat/private_key_providers/test/test_data/rsa-4096.pem" }
        )EOF";

  EXPECT_THROW_WITH_REGEX(createWithConfig(yaml), EnvoyException,
                          "Proto constraint validation failed");
}

TEST_F(QatConfigTest, CreateZeroPollDelay) {
  const std::string yaml = R"EOF(
      provider_name: qat
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.qat.v3alpha.QatPrivateKeyMethodConfig
        poll_delay: 0s
        private_key: { "filename": "{{ test_rundir }}/contrib/qat/private_key_providers/test/test_data/rsa-4096.pem" }
        )EOF";

  EXPECT_THROW_WITH_REGEX(createWithConfig(yaml), EnvoyException,
                          "Proto constraint validation failed");
}

} // namespace Qat
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
