#include <string>

#include "source/common/common/random_generator.h"
#include "source/common/tls/private_key/private_key_manager_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/common.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/registry.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "contrib/kae/private_key_providers/source/kae_private_key_provider.h"
#include "fake_factory.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Kae {

envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider
parsePrivateKeyProviderFromV3Yaml(const std::string& yaml_string) {
  envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider private_key_provider;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml_string), private_key_provider);
  return private_key_provider;
}

class FakeSingletonManager : public Singleton::Manager {
public:
  FakeSingletonManager(LibUadkCryptoSharedPtr libuadk) : libuadk_(libuadk) {}
  Singleton::InstanceSharedPtr get(const std::string&, Singleton::SingletonFactoryCb,
                                   bool) override {
    return std::make_shared<KaeManager>(libuadk_);
  }

private:
  LibUadkCryptoSharedPtr libuadk_;
};

class KaeConfigTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  KaeConfigTest()
      : api_(Api::createApiForTest(store_, time_system_)),
        libuadk_(std::make_shared<FakeLibUadkCryptoImpl>()), fsm_(libuadk_) {
    ON_CALL(factory_context_.server_context_, api()).WillByDefault(ReturnRef(*api_));
    ON_CALL(factory_context_.server_context_, sslContextManager())
        .WillByDefault(ReturnRef(context_manager_));
    ON_CALL(context_manager_, privateKeyMethodManager())
        .WillByDefault(ReturnRef(private_key_method_manager_));
    ON_CALL(factory_context_.server_context_, singletonManager()).WillByDefault(ReturnRef(fsm_));
  }

  Ssl::PrivateKeyMethodProviderSharedPtr createWithConfig(std::string yaml) {
    FakeKaePrivateKeyMethodFactory kae_factory;
    Registry::InjectFactory<Ssl::PrivateKeyMethodProviderInstanceFactory>
        kae_private_key_method_factory(kae_factory);

    return factory_context_.serverFactoryContext()
        .sslContextManager()
        .privateKeyMethodManager()
        .createPrivateKeyMethodProvider(parsePrivateKeyProviderFromV3Yaml(yaml), factory_context_);
  }

  Event::SimulatedTimeSystem time_system_;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
  Stats::IsolatedStoreImpl store_;
  Api::ApiPtr api_;
  NiceMock<Ssl::MockContextManager> context_manager_;
  TransportSockets::Tls::PrivateKeyMethodManagerImpl private_key_method_manager_;
  std::shared_ptr<FakeLibUadkCryptoImpl> libuadk_;
  FakeSingletonManager fsm_;
};

TEST_F(KaeConfigTest, CreateRsa1024) {
  const std::string yaml = R"EOF(
      provider_name: kae
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.kae.v3alpha.KaePrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key: { "filename": "{{ test_rundir }}/contrib/kae/private_key_providers/test/test_data/rsa-1024.pem" }
)EOF";

  Ssl::PrivateKeyMethodProviderSharedPtr provider = createWithConfig(yaml);
  EXPECT_NE(nullptr, provider);
  EXPECT_EQ(false, provider->checkFips());
  EXPECT_EQ(provider->isAvailable(), true);
  Ssl::BoringSslPrivateKeyMethodSharedPtr method = provider->getBoringSslPrivateKeyMethod();
  EXPECT_NE(nullptr, method);
}

TEST_F(KaeConfigTest, CreateRsa2048) {
  const std::string yaml = R"EOF(
      provider_name: kae
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.kae.v3alpha.KaePrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key: { "filename": "{{ test_rundir }}/contrib/kae/private_key_providers/test/test_data/rsa-2048.pem" }
)EOF";

  Ssl::PrivateKeyMethodProviderSharedPtr provider = createWithConfig(yaml);
  EXPECT_NE(nullptr, provider);
  EXPECT_EQ(provider->isAvailable(), true);
}

TEST_F(KaeConfigTest, CreateRsa3072) {
  const std::string yaml = R"EOF(
      provider_name: kae
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.kae.v3alpha.KaePrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key: { "filename": "{{ test_rundir }}/contrib/kae/private_key_providers/test/test_data/rsa-3072.pem" }
)EOF";

  Ssl::PrivateKeyMethodProviderSharedPtr provider = createWithConfig(yaml);
  EXPECT_NE(nullptr, provider);
  EXPECT_EQ(provider->isAvailable(), true);
}

TEST_F(KaeConfigTest, CreateRsa4096) {
  const std::string yaml = R"EOF(
      provider_name: kae
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.kae.v3alpha.KaePrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key: { "filename": "{{ test_rundir }}/contrib/kae/private_key_providers/test/test_data/rsa-4096.pem" }
)EOF";

  Ssl::PrivateKeyMethodProviderSharedPtr provider = createWithConfig(yaml);
  EXPECT_NE(nullptr, provider);
  EXPECT_EQ(provider->isAvailable(), true);
}

TEST_F(KaeConfigTest, CreateEcdsaP256) {
  const std::string yaml = R"EOF(
      provider_name: kae
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.kae.v3alpha.KaePrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key: { "filename": "{{ test_rundir }}/contrib/kae/private_key_providers/test/test_data/ecdsa-p256.pem" }
)EOF";

  Ssl::PrivateKeyMethodProviderSharedPtr provider = createWithConfig(yaml);
  EXPECT_NE(nullptr, provider);
  EXPECT_EQ(provider->isAvailable(), false);
}

TEST_F(KaeConfigTest, CreateMissingPrivateKeyFile) {
  const std::string yaml = R"EOF(
      provider_name: kae
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.kae.v3alpha.KaePrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key: { "filename": "{{ test_rundir }}/contrib/kae/private_key_providers/test/test_data/missing.pem" }
)EOF";

  EXPECT_THROW(createWithConfig(yaml), EnvoyException);
}

TEST_F(KaeConfigTest, CreateMissingKey) {
  const std::string yaml = R"EOF(
      provider_name: kae
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.kae.v3alpha.KaePrivateKeyMethodConfig
        poll_delay: 0.02s
        )EOF";

  EXPECT_THROW_WITH_MESSAGE(createWithConfig(yaml), EnvoyException,
                            "Unexpected DataSource::specifier_case(): 0");
}

TEST_F(KaeConfigTest, CreateMissingPollDelay) {
  const std::string yaml = R"EOF(
      provider_name: kae
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.kae.v3alpha.KaePrivateKeyMethodConfig
        private_key: { "filename": "{{ test_rundir }}/contrib/kae/private_key_providers/test/test_data/rsa-4096.pem" }
        )EOF";

  EXPECT_THROW_WITH_REGEX(createWithConfig(yaml), EnvoyException,
                          "Proto constraint validation failed");
}

TEST_F(KaeConfigTest, CreateZeroPollDelay) {
  const std::string yaml = R"EOF(
      provider_name: kae
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.kae.v3alpha.KaePrivateKeyMethodConfig
        poll_delay: 0s
        private_key: { "filename": "{{ test_rundir }}/contrib/kae/private_key_providers/test/test_data/rsa-4096.pem" }
        )EOF";

  EXPECT_THROW_WITH_REGEX(createWithConfig(yaml), EnvoyException,
                          "Proto constraint validation failed");
}

} // namespace Kae
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
