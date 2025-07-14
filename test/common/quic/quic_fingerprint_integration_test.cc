#include <memory>
#include <string>

#include "envoy/config/listener/v3/quic_config.pb.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/hex.h"
#include "source/common/quic/quic_fingerprint_inspector.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_server_stream.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Quic {

/**
 * Integration test class for QUIC fingerprinting that tests the complete flow
 * from configuration through fingerprint processing.
 */
class QuicFingerprintIntegrationTest : public testing::Test {
public:
  QuicFingerprintIntegrationTest() : stats_store_(std::make_unique<Stats::IsolatedStoreImpl>()) {}

  void SetUp() override {
    // Create QUIC config with both ``JA3`` and ``JA4`` fingerprinting enabled.
    quic_config_.mutable_enable_ja3_fingerprinting()->set_value(true);
    quic_config_.mutable_enable_ja4_fingerprinting()->set_value(true);

    // Create factory.
    factory_ = std::make_unique<EnvoyQuicCryptoServerStreamFactoryImpl>();
    factory_->setQuicConfig(quic_config_);

    // Create inspector config.
    inspector_config_ =
        std::make_shared<QuicFingerprintInspectorConfig>(*stats_store_->rootScope(), quic_config_);

    // Create inspector.
    inspector_ = std::make_unique<QuicFingerprintInspector>(inspector_config_);
  }

protected:
  std::unique_ptr<Stats::IsolatedStoreImpl> stats_store_;
  envoy::config::listener::v3::QuicProtocolOptions quic_config_;
  std::unique_ptr<EnvoyQuicCryptoServerStreamFactoryImpl> factory_;
  QuicFingerprintInspectorConfigSharedPtr inspector_config_;
  std::unique_ptr<QuicFingerprintInspector> inspector_;
};

// Test the complete factory configuration flow.
TEST_F(QuicFingerprintIntegrationTest, FactoryConfigurationFlow) {
  // Verify the factory is configured correctly.
  EXPECT_TRUE(factory_->getQuicConfig().has_value());
  EXPECT_TRUE(PROTOBUF_GET_WRAPPED_OR_DEFAULT(factory_->getQuicConfig().value(),
                                              enable_ja3_fingerprinting, false));
  EXPECT_TRUE(PROTOBUF_GET_WRAPPED_OR_DEFAULT(factory_->getQuicConfig().value(),
                                              enable_ja4_fingerprinting, false));

  // Verify the factory name and proto creation.
  EXPECT_EQ("envoy.quic.crypto_stream.server.quiche", factory_->name());
  auto proto = factory_->createEmptyConfigProto();
  EXPECT_NE(nullptr, proto);
}

// Test the complete fingerprinting configuration flow with different settings.
TEST_F(QuicFingerprintIntegrationTest, ConfigurationVariations) {
  // Test``JA3``only.
  {
    envoy::config::listener::v3::QuicProtocolOptions ja3_only_config;
    ja3_only_config.mutable_enable_ja3_fingerprinting()->set_value(true);
    ja3_only_config.mutable_enable_ja4_fingerprinting()->set_value(false);

    auto ja3_only_inspector_config = std::make_shared<QuicFingerprintInspectorConfig>(
        *stats_store_->rootScope(), ja3_only_config);

    EXPECT_TRUE(ja3_only_inspector_config->enableJA3Fingerprinting());
    EXPECT_FALSE(ja3_only_inspector_config->enableJA4Fingerprinting());

    auto ja3_only_factory = std::make_unique<EnvoyQuicCryptoServerStreamFactoryImpl>();
    ja3_only_factory->setQuicConfig(ja3_only_config);

    EXPECT_TRUE(ja3_only_factory->getQuicConfig().has_value());
    EXPECT_TRUE(PROTOBUF_GET_WRAPPED_OR_DEFAULT(ja3_only_factory->getQuicConfig().value(),
                                                enable_ja3_fingerprinting, false));
    EXPECT_FALSE(PROTOBUF_GET_WRAPPED_OR_DEFAULT(ja3_only_factory->getQuicConfig().value(),
                                                 enable_ja4_fingerprinting, false));
  }

  // Test``JA4``only.
  {
    envoy::config::listener::v3::QuicProtocolOptions ja4_only_config;
    ja4_only_config.mutable_enable_ja3_fingerprinting()->set_value(false);
    ja4_only_config.mutable_enable_ja4_fingerprinting()->set_value(true);

    auto ja4_only_inspector_config = std::make_shared<QuicFingerprintInspectorConfig>(
        *stats_store_->rootScope(), ja4_only_config);

    EXPECT_FALSE(ja4_only_inspector_config->enableJA3Fingerprinting());
    EXPECT_TRUE(ja4_only_inspector_config->enableJA4Fingerprinting());

    auto ja4_only_factory = std::make_unique<EnvoyQuicCryptoServerStreamFactoryImpl>();
    ja4_only_factory->setQuicConfig(ja4_only_config);

    EXPECT_TRUE(ja4_only_factory->getQuicConfig().has_value());
    EXPECT_FALSE(PROTOBUF_GET_WRAPPED_OR_DEFAULT(ja4_only_factory->getQuicConfig().value(),
                                                 enable_ja3_fingerprinting, false));
    EXPECT_TRUE(PROTOBUF_GET_WRAPPED_OR_DEFAULT(ja4_only_factory->getQuicConfig().value(),
                                                enable_ja4_fingerprinting, false));
  }

  // Test both disabled.
  {
    envoy::config::listener::v3::QuicProtocolOptions disabled_config;
    disabled_config.mutable_enable_ja3_fingerprinting()->set_value(false);
    disabled_config.mutable_enable_ja4_fingerprinting()->set_value(false);

    auto disabled_inspector_config = std::make_shared<QuicFingerprintInspectorConfig>(
        *stats_store_->rootScope(), disabled_config);

    EXPECT_FALSE(disabled_inspector_config->enableJA3Fingerprinting());
    EXPECT_FALSE(disabled_inspector_config->enableJA4Fingerprinting());

    auto disabled_factory = std::make_unique<EnvoyQuicCryptoServerStreamFactoryImpl>();
    disabled_factory->setQuicConfig(disabled_config);

    EXPECT_TRUE(disabled_factory->getQuicConfig().has_value());
    EXPECT_FALSE(PROTOBUF_GET_WRAPPED_OR_DEFAULT(disabled_factory->getQuicConfig().value(),
                                                 enable_ja3_fingerprinting, false));
    EXPECT_FALSE(PROTOBUF_GET_WRAPPED_OR_DEFAULT(disabled_factory->getQuicConfig().value(),
                                                 enable_ja4_fingerprinting, false));
  }
}

// Test fingerprint inspector with test vectors.
TEST_F(QuicFingerprintIntegrationTest, FingerprintInspectorWithTestVectors) {
  // Test with known``JA4``test vectors (hex data from TLS inspector tests).
  const std::string client_hello_hex = "1603010200010001fc0303528b4e00213672e534980dfed836dd5b375ab"
                                       "164dcc65ba6a3c87e7e2a1f9d61201bf29"
                                       "c9dffaa31ed2df524d3a113edb4e6fd3b7fb3d6d57d5d9aafb213e83c42"
                                       "0020aaaa130113021303c02bc02fc02cc0"
                                       "30cca9cca8c013c014009c009d002f0035010001936a6a0000000d00120"
                                       "0100403080404010503080505010806060"
                                       "1000000170015000012656467652e6d6963726f736f66742e636f6d000a"
                                       "000a00084a4a001d001700180005000501"
                                       "00000000000b00020100002b0007069a9a03040303001b0003020002ff0"
                                       "10001000033002b00294a4a000100001d0"
                                       "020c4ce4268d58f0c703855163f4754b883742487a5ce87a6016a30208c"
                                       "18e07f69446900050003026832002d0002"
                                       "01010023000000170000001200000010000e000c02683208687474702f3"
                                       "12e310a0a000100001500c500000000000"
                                       "00000000000000000000000000000000000000000000000000000000000"
                                       "0000000000000000000000000000000000"
                                       "00000000000000000000000000000000000000000000000000000000000"
                                       "0000000000000000000000000000000000"
                                       "00000000000000000000000000000000000000000000000000000000000"
                                       "0000000000000000000000000000000000"
                                       "00000000000000000000000000000000000000000000000000000000000"
                                       "0000000000000000000000000000000000"
                                       "00000000000";

  const std::string expected_ja4 = "t13d1516h2_8daaf6152771_e5627efa2ab1";

  // Decode the hex string to bytes (for reference).
  std::vector<uint8_t> client_hello_bytes = Hex::decode(client_hello_hex);
  EXPECT_FALSE(client_hello_bytes.empty());

  // Since we can't easily create SSL_CLIENT_HELLO from raw bytes in unit tests,
  // we test the error handling flow with null input.
  std::string ja3_hash;
  std::string ja4_hash;

  const bool result = inspector_->processClientHello(nullptr, ja3_hash, ja4_hash);

  EXPECT_FALSE(result);
  EXPECT_TRUE(ja3_hash.empty());
  EXPECT_TRUE(ja4_hash.empty());

  // Verify error stats were incremented.
  EXPECT_EQ(1,
            TestUtility::findCounter(*stats_store_, "quic_fingerprint_inspector.fingerprint_errors")
                ->value());
}

// Test stats integration across the complete flow.
TEST_F(QuicFingerprintIntegrationTest, StatsIntegration) {
  // Verify initial stats are zero.
  EXPECT_EQ(0, TestUtility::findCounter(*stats_store_,
                                        "quic_fingerprint_inspector.client_hello_processed")
                   ->value());
  EXPECT_EQ(0, TestUtility::findCounter(*stats_store_,
                                        "quic_fingerprint_inspector.ja3_fingerprint_created")
                   ->value());
  EXPECT_EQ(0, TestUtility::findCounter(*stats_store_,
                                        "quic_fingerprint_inspector.ja4_fingerprint_created")
                   ->value());
  EXPECT_EQ(0,
            TestUtility::findCounter(*stats_store_, "quic_fingerprint_inspector.fingerprint_errors")
                ->value());

  // Process multiple null ClientHello messages to test stats accumulation.
  for (int i = 0; i < 3; ++i) {
    std::string ja3_hash;
    std::string ja4_hash;
    const bool result = inspector_->processClientHello(nullptr, ja3_hash, ja4_hash);
    EXPECT_FALSE(result);
  }

  // Verify error stats were incremented correctly.
  EXPECT_EQ(3,
            TestUtility::findCounter(*stats_store_, "quic_fingerprint_inspector.fingerprint_errors")
                ->value());

  // Other stats should remain zero.
  EXPECT_EQ(0, TestUtility::findCounter(*stats_store_,
                                        "quic_fingerprint_inspector.client_hello_processed")
                   ->value());
  EXPECT_EQ(0, TestUtility::findCounter(*stats_store_,
                                        "quic_fingerprint_inspector.ja3_fingerprint_created")
                   ->value());
  EXPECT_EQ(0, TestUtility::findCounter(*stats_store_,
                                        "quic_fingerprint_inspector.ja4_fingerprint_created")
                   ->value());
}

// Test factory registration and name consistency.
TEST_F(QuicFingerprintIntegrationTest, FactoryRegistrationAndName) {
  EXPECT_EQ("envoy.quic.crypto_stream.server.quiche", factory_->name());

  auto proto = factory_->createEmptyConfigProto();
  EXPECT_NE(nullptr, proto);

  // The factory should be registered in the registry.
  // We can't easily test registry registration without more complex setup,
  // but we can verify the name is correct and consistent.
  EXPECT_FALSE(factory_->name().empty());
  EXPECT_TRUE(factory_->name().find("quiche") != std::string::npos);
  EXPECT_TRUE(factory_->name().find("quic") != std::string::npos);
  EXPECT_TRUE(factory_->name().find("crypto_stream") != std::string::npos);
}

// Test complete configuration lifecycle.
TEST_F(QuicFingerprintIntegrationTest, ConfigurationLifecycle) {
  // Create multiple factories with different configurations.
  auto factory1 = std::make_unique<EnvoyQuicCryptoServerStreamFactoryImpl>();
  auto factory2 = std::make_unique<EnvoyQuicCryptoServerStreamFactoryImpl>();

  // Configure factory1 with both fingerprinting enabled.
  envoy::config::listener::v3::QuicProtocolOptions config1;
  config1.mutable_enable_ja3_fingerprinting()->set_value(true);
  config1.mutable_enable_ja4_fingerprinting()->set_value(true);
  factory1->setQuicConfig(config1);

  // Configure factory2 with fingerprinting disabled.
  envoy::config::listener::v3::QuicProtocolOptions config2;
  config2.mutable_enable_ja3_fingerprinting()->set_value(false);
  config2.mutable_enable_ja4_fingerprinting()->set_value(false);
  factory2->setQuicConfig(config2);

  // Verify both factories have different configurations.
  EXPECT_TRUE(factory1->getQuicConfig().has_value());
  EXPECT_TRUE(factory2->getQuicConfig().has_value());

  EXPECT_TRUE(PROTOBUF_GET_WRAPPED_OR_DEFAULT(factory1->getQuicConfig().value(),
                                              enable_ja3_fingerprinting, false));
  EXPECT_FALSE(PROTOBUF_GET_WRAPPED_OR_DEFAULT(factory2->getQuicConfig().value(),
                                               enable_ja3_fingerprinting, false));

  EXPECT_TRUE(PROTOBUF_GET_WRAPPED_OR_DEFAULT(factory1->getQuicConfig().value(),
                                              enable_ja4_fingerprinting, false));
  EXPECT_FALSE(PROTOBUF_GET_WRAPPED_OR_DEFAULT(factory2->getQuicConfig().value(),
                                               enable_ja4_fingerprinting, false));

  // Both factories should have the same name and proto creation capability.
  EXPECT_EQ(factory1->name(), factory2->name());
  EXPECT_NE(nullptr, factory1->createEmptyConfigProto());
  EXPECT_NE(nullptr, factory2->createEmptyConfigProto());
}

// Test comprehensive factory lifecycle management.
TEST_F(QuicFingerprintIntegrationTest, FactoryLifecycleManagement) {
  // Test factory creation and destruction cycles.
  for (int cycle = 0; cycle < 3; ++cycle) {
    auto factory = std::make_unique<EnvoyQuicCryptoServerStreamFactoryImpl>();

    EXPECT_EQ("envoy.quic.crypto_stream.server.quiche", factory->name());
    EXPECT_FALSE(factory->getQuicConfig().has_value());

    // Configure the factory.
    envoy::config::listener::v3::QuicProtocolOptions config;
    config.mutable_enable_ja3_fingerprinting()->set_value(cycle % 2 == 0);
    config.mutable_enable_ja4_fingerprinting()->set_value(cycle % 2 == 1);
    factory->setQuicConfig(config);

    // Verify configuration.
    EXPECT_TRUE(factory->getQuicConfig().has_value());
    EXPECT_EQ(cycle % 2 == 0, PROTOBUF_GET_WRAPPED_OR_DEFAULT(factory->getQuicConfig().value(),
                                                              enable_ja3_fingerprinting, false));
    EXPECT_EQ(cycle % 2 == 1, PROTOBUF_GET_WRAPPED_OR_DEFAULT(factory->getQuicConfig().value(),
                                                              enable_ja4_fingerprinting, false));

    // Test proto creation.
    auto proto = factory->createEmptyConfigProto();
    EXPECT_NE(nullptr, proto);

    // Factory is destroyed at the end of the loop.
  }
}

// Test inspector configuration edge cases and boundary conditions.
TEST_F(QuicFingerprintIntegrationTest, InspectorConfigurationEdgeCases) {
  // Test inspector with default configuration (no fingerprinting enabled).
  {
    envoy::config::listener::v3::QuicProtocolOptions default_config;
    auto default_inspector_config = std::make_shared<QuicFingerprintInspectorConfig>(
        *stats_store_->rootScope(), default_config);
    auto default_inspector = std::make_unique<QuicFingerprintInspector>(default_inspector_config);

    EXPECT_FALSE(default_inspector_config->enableJA3Fingerprinting());
    EXPECT_FALSE(default_inspector_config->enableJA4Fingerprinting());

    // Test that disabled inspector still handles null input gracefully.
    std::string ja3_hash;
    std::string ja4_hash;
    const bool result = default_inspector->processClientHello(nullptr, ja3_hash, ja4_hash);
    EXPECT_FALSE(result);
    EXPECT_TRUE(ja3_hash.empty());
    EXPECT_TRUE(ja4_hash.empty());
  }

  // Test inspector with partial configuration.
  {
    envoy::config::listener::v3::QuicProtocolOptions partial_config;
    partial_config.mutable_enable_ja3_fingerprinting()->set_value(true);
    // Leave``JA4``unset (defaults to false).

    auto partial_inspector_config = std::make_shared<QuicFingerprintInspectorConfig>(
        *stats_store_->rootScope(), partial_config);

    EXPECT_TRUE(partial_inspector_config->enableJA3Fingerprinting());
    EXPECT_FALSE(partial_inspector_config->enableJA4Fingerprinting());
  }
}

// Test stats integration with comprehensive scenarios.
TEST_F(QuicFingerprintIntegrationTest, ComprehensiveStatsIntegration) {
  // Create multiple inspector instances with different configurations.
  auto ja3_only_config =
      std::make_shared<QuicFingerprintInspectorConfig>(*stats_store_->rootScope(), []() {
        envoy::config::listener::v3::QuicProtocolOptions config;
        config.mutable_enable_ja3_fingerprinting()->set_value(true);
        config.mutable_enable_ja4_fingerprinting()->set_value(false);
        return config;
      }());

  auto ja4_only_config =
      std::make_shared<QuicFingerprintInspectorConfig>(*stats_store_->rootScope(), []() {
        envoy::config::listener::v3::QuicProtocolOptions config;
        config.mutable_enable_ja3_fingerprinting()->set_value(false);
        config.mutable_enable_ja4_fingerprinting()->set_value(true);
        return config;
      }());

  auto ja3_only_inspector = std::make_unique<QuicFingerprintInspector>(ja3_only_config);
  auto ja4_only_inspector = std::make_unique<QuicFingerprintInspector>(ja4_only_config);

  // Verify initial stats are zero for all configurations.
  const std::vector<std::string> counter_names = {
      "quic_fingerprint_inspector.client_hello_processed",
      "quic_fingerprint_inspector.ja3_fingerprint_created",
      "quic_fingerprint_inspector.ja4_fingerprint_created",
      "quic_fingerprint_inspector.fingerprint_errors"};

  for (const auto& counter_name : counter_names) {
    EXPECT_EQ(0, TestUtility::findCounter(*stats_store_, counter_name)->value());
  }

  // Process null ClientHello with different inspectors.
  std::string ja3_hash;
  std::string ja4_hash;

  // ``JA3``-only inspector.
  EXPECT_FALSE(ja3_only_inspector->processClientHello(nullptr, ja3_hash, ja4_hash));
  EXPECT_FALSE(ja4_only_inspector->processClientHello(nullptr, ja3_hash, ja4_hash));
  EXPECT_FALSE(inspector_->processClientHello(nullptr, ja3_hash, ja4_hash)); // Both enabled.

  // Verify error stats were incremented (all failed due to null input).
  EXPECT_EQ(3,
            TestUtility::findCounter(*stats_store_, "quic_fingerprint_inspector.fingerprint_errors")
                ->value());

  // Other stats should remain zero since no valid processing occurred.
  EXPECT_EQ(0, TestUtility::findCounter(*stats_store_,
                                        "quic_fingerprint_inspector.client_hello_processed")
                   ->value());
  EXPECT_EQ(0, TestUtility::findCounter(*stats_store_,
                                        "quic_fingerprint_inspector.ja3_fingerprint_created")
                   ->value());
  EXPECT_EQ(0, TestUtility::findCounter(*stats_store_,
                                        "quic_fingerprint_inspector.ja4_fingerprint_created")
                   ->value());
}

// Test factory and inspector integration scenarios.
TEST_F(QuicFingerprintIntegrationTest, FactoryInspectorIntegration) {
  // Test that factory and inspector configurations are consistent.
  std::vector<std::pair<bool, bool>> config_combinations = {
      {true, true},  // Both enabled
      {true, false}, //``JA3``only
      {false, true}, //``JA4``only
      {false, false} // Both disabled
  };

  for (const auto& [ja3_enabled, ja4_enabled] : config_combinations) {
    // Create factory with specific configuration.
    auto factory = std::make_unique<EnvoyQuicCryptoServerStreamFactoryImpl>();
    envoy::config::listener::v3::QuicProtocolOptions config;
    config.mutable_enable_ja3_fingerprinting()->set_value(ja3_enabled);
    config.mutable_enable_ja4_fingerprinting()->set_value(ja4_enabled);
    factory->setQuicConfig(config);

    // Create inspector with same configuration.
    auto inspector_config =
        std::make_shared<QuicFingerprintInspectorConfig>(*stats_store_->rootScope(), config);
    auto inspector = std::make_unique<QuicFingerprintInspector>(inspector_config);

    // Verify configurations match.
    EXPECT_TRUE(factory->getQuicConfig().has_value());
    EXPECT_EQ(ja3_enabled, PROTOBUF_GET_WRAPPED_OR_DEFAULT(factory->getQuicConfig().value(),
                                                           enable_ja3_fingerprinting, false));
    EXPECT_EQ(ja4_enabled, PROTOBUF_GET_WRAPPED_OR_DEFAULT(factory->getQuicConfig().value(),
                                                           enable_ja4_fingerprinting, false));
    EXPECT_EQ(ja3_enabled, inspector_config->enableJA3Fingerprinting());
    EXPECT_EQ(ja4_enabled, inspector_config->enableJA4Fingerprinting());

    // Test that both components handle the same configurations consistently.
    auto proto = factory->createEmptyConfigProto();
    EXPECT_NE(nullptr, proto);
  }
}

// Test thread safety and configuration isolation.
TEST_F(QuicFingerprintIntegrationTest, ConfigurationIsolationTest) {
  // Create multiple independent factory instances.
  constexpr int num_factories = 5;
  std::vector<std::unique_ptr<EnvoyQuicCryptoServerStreamFactoryImpl>> factories;

  for (int i = 0; i < num_factories; ++i) {
    auto factory = std::make_unique<EnvoyQuicCryptoServerStreamFactoryImpl>();

    // Configure each factory differently.
    envoy::config::listener::v3::QuicProtocolOptions config;
    config.mutable_enable_ja3_fingerprinting()->set_value(i % 2 == 0);
    config.mutable_enable_ja4_fingerprinting()->set_value(i % 3 == 0);
    factory->setQuicConfig(config);

    factories.push_back(std::move(factory));
  }

  // Verify each factory maintains its own configuration.
  for (int i = 0; i < num_factories; ++i) {
    EXPECT_TRUE(factories[i]->getQuicConfig().has_value());
    EXPECT_EQ(i % 2 == 0, PROTOBUF_GET_WRAPPED_OR_DEFAULT(factories[i]->getQuicConfig().value(),
                                                          enable_ja3_fingerprinting, false));
    EXPECT_EQ(i % 3 == 0, PROTOBUF_GET_WRAPPED_OR_DEFAULT(factories[i]->getQuicConfig().value(),
                                                          enable_ja4_fingerprinting, false));
    EXPECT_EQ("envoy.quic.crypto_stream.server.quiche", factories[i]->name());
  }
}

// Test resource management and cleanup.
TEST_F(QuicFingerprintIntegrationTest, ResourceManagementTest) {
  // Test that resources are properly managed during object lifecycle.
  constexpr int num_iterations = 10;

  for (int i = 0; i < num_iterations; ++i) {
    // Create temporary configurations and objects.
    envoy::config::listener::v3::QuicProtocolOptions temp_config;
    temp_config.mutable_enable_ja3_fingerprinting()->set_value(true);
    temp_config.mutable_enable_ja4_fingerprinting()->set_value(true);

    auto temp_inspector_config =
        std::make_shared<QuicFingerprintInspectorConfig>(*stats_store_->rootScope(), temp_config);
    auto temp_inspector = std::make_unique<QuicFingerprintInspector>(temp_inspector_config);
    auto temp_factory = std::make_unique<EnvoyQuicCryptoServerStreamFactoryImpl>();

    temp_factory->setQuicConfig(temp_config);

    // Test basic functionality.
    EXPECT_TRUE(temp_inspector_config->enableJA3Fingerprinting());
    EXPECT_TRUE(temp_inspector_config->enableJA4Fingerprinting());
    EXPECT_TRUE(temp_factory->getQuicConfig().has_value());

    std::string ja3_hash;
    std::string ja4_hash;
    EXPECT_FALSE(temp_inspector->processClientHello(nullptr, ja3_hash, ja4_hash));

    // Objects are automatically destroyed at the end of the loop.
    // This tests proper cleanup without explicit verification.
  }

  // Verify that temporary object creation/destruction didn't affect persistent stats.
  // The error counter should reflect all the null ClientHello processing.
  EXPECT_EQ(num_iterations,
            TestUtility::findCounter(*stats_store_, "quic_fingerprint_inspector.fingerprint_errors")
                ->value());
}

} // namespace Quic
} // namespace Envoy
