#include <string>

#include "source/common/common/random_generator.h"
#include "source/extensions/private_key_providers/cryptomb/config.h"
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

#include "fake_factory.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyProviders {
namespace CryptoMb {

inline envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider
parsePrivateKeyProviderFromV3Yaml(const std::string& yaml_string) {
  envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider private_key_provider;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml_string), private_key_provider);
  return private_key_provider;
}

PrivateKeyMethodProvider::CryptoMbPrivateKeyMethodProvider*
createWithConfig(std::string yaml, bool supported_instruction_set = true) {
  FakeCryptoMbPrivateKeyMethodFactory cryptomb_factory(supported_instruction_set);
  Registry::InjectFactory<Ssl::PrivateKeyMethodProviderInstanceFactory>
      cryptomb_private_key_method_factory(cryptomb_factory);

  Event::SimulatedTimeSystem time_system;
  Stats::TestUtil::TestStore server_stats_store;
  Api::ApiPtr server_api = Api::createApiForTest(server_stats_store, time_system);
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>
      server_factory_context;
  testing::NiceMock<ThreadLocal::MockInstance> tls;
  TransportSockets::Tls::PrivateKeyMethodManagerImpl private_key_method_manager;
  NiceMock<Ssl::MockContextManager> context_manager;

  ON_CALL(server_factory_context, api()).WillByDefault(testing::ReturnRef(*server_api));
  ON_CALL(server_factory_context, threadLocal()).WillByDefault(testing::ReturnRef(tls));
  EXPECT_CALL(server_factory_context, sslContextManager())
      .WillOnce(testing::ReturnRef(context_manager))
      .WillRepeatedly(testing::ReturnRef(context_manager));
  EXPECT_CALL(context_manager, privateKeyMethodManager())
      .WillOnce(testing::ReturnRef(private_key_method_manager))
      .WillRepeatedly(testing::ReturnRef(private_key_method_manager));

  return dynamic_cast<PrivateKeyMethodProvider::CryptoMbPrivateKeyMethodProvider*>(
      server_factory_context.sslContextManager()
          .privateKeyMethodManager()
          .createPrivateKeyMethodProvider(parsePrivateKeyProviderFromV3Yaml(yaml),
                                          server_factory_context)
          .get());
}

TEST(CryptoMbConfigTest, CreateRsa4096) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        poll_delay: 1
        private_key_file: "{{ test_rundir }}/test/extensions/private_key_providers/crypto_mb/test_data/rsa-4096.pem"
)EOF";

  EXPECT_NE(nullptr, createWithConfig(yaml));
}

TEST(CryptoMbConfigTest, CreateRsa512) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        poll_delay: 1
        private_key_file: "{{ test_rundir }}/test/extensions/private_key_providers/crypto_mb/test_data/rsa-512.pem"
)EOF";

  EXPECT_THROW_WITH_MESSAGE(createWithConfig(yaml), EnvoyException,
                            "Only RSA keys of 1024, 2048, 3072, and 4096 bits are supported.");
}

TEST(CryptoMbConfigTest, CreateEcdsaP256) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        poll_delay: 1
        private_key_file: "{{ test_rundir }}/test/extensions/private_key_providers/crypto_mb/test_data/ecdsa-p256.pem"
)EOF";

  EXPECT_NE(nullptr, createWithConfig(yaml));
}

TEST(CryptoMbConfigTest, CreateEcdsaP256Inline) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        poll_delay: 1
        inline_private_key: |
          -----BEGIN PRIVATE KEY-----
          MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgIxp5QZ3YFaT8s+CR
          rqUqeYSe5D9APgBZbyCvAkO2/JChRANCAARM53DFLHORcSyBpu5zpaG7/HfLXT8H
          r1RaoGEiH9pi3MIKg1H+b8EaM1M4wURT2yXMjuvogQ6ixs0B1mvRkZnL
          -----END PRIVATE KEY-----
)EOF";

  EXPECT_NE(nullptr, createWithConfig(yaml));
}

TEST(CryptoMbConfigTest, CreateEcdsaP384) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        poll_delay: 1
        private_key_file: "{{ test_rundir }}/test/extensions/private_key_providers/crypto_mb/test_data/ecdsa-p384.pem"
)EOF";

  EXPECT_THROW_WITH_MESSAGE(createWithConfig(yaml), EnvoyException,
                            "Only P-256 ECDSA keys are supported.");
}

TEST(CryptoMbConfigTest, CreateMissingPrivateKey) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        poll_delay: 1
        private_key_file: "{{ test_rundir }}/test/extensions/private_key_providers/crypto_mb/test_data/missing.pem"
)EOF";

  EXPECT_THROW(createWithConfig(yaml), EnvoyException);
}

TEST(CryptoMbConfigTest, CreateBothInlineAndFile) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        poll_delay: 1
        private_key_file: "{{ test_rundir }}/test/extensions/private_key_providers/crypto_mb/test_data/rsa-4096.pem"
        inline_private_key: |
          -----BEGIN PRIVATE KEY-----
          MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgIxp5QZ3YFaT8s+CR
          rqUqeYSe5D9APgBZbyCvAkO2/JChRANCAARM53DFLHORcSyBpu5zpaG7/HfLXT8H
          r1RaoGEiH9pi3MIKg1H+b8EaM1M4wURT2yXMjuvogQ6ixs0B1mvRkZnL
          -----END PRIVATE KEY-----
        )EOF";

  EXPECT_THROW(createWithConfig(yaml), EnvoyException);
}

TEST(CryptoMbConfigTest, CreateMissingKey) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        poll_delay: 1
        )EOF";

  EXPECT_THROW_WITH_MESSAGE(createWithConfig(yaml), EnvoyException,
                            "Exactly one of private_key or inline_private_key must be configured.");
}

TEST(CryptoMbConfigTest, CreateMissingPollDelay) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        private_key_file: "{{ test_rundir }}/test/extensions/private_key_providers/crypto_mb/test_data/rsa-4096.pem"
        )EOF";

  // Default value 0 means that just one request is processed at once.
  EXPECT_NE(nullptr, createWithConfig(yaml));
}

TEST(CryptoMbConfigTest, CreateNotSupportedInstructionSet) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        private_key_file: "{{ test_rundir }}/test/extensions/private_key_providers/crypto_mb/test_data/rsa-4096.pem"
        poll_delay: 1
        )EOF";

  EXPECT_THROW_WITH_MESSAGE(createWithConfig(yaml, false), EnvoyException,
                            "Multi-buffer CPU instructions not available.");
}
} // namespace CryptoMb
} // namespace PrivateKeyProviders
} // namespace Extensions
} // namespace Envoy
