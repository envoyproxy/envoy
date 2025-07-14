#include <memory>
#include <string>

#include "envoy/config/listener/v3/quic_config.pb.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/quic/quic_fingerprint_inspector.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_server_stream.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Quic {

class QuicFingerprintInspectorTest : public testing::Test {
public:
  QuicFingerprintInspectorTest() : stats_store_(std::make_unique<Stats::IsolatedStoreImpl>()) {}

  void SetUp() override {
    // Create a QUIC config with fingerprinting enabled.
    quic_config_.mutable_enable_ja3_fingerprinting()->set_value(true);
    quic_config_.mutable_enable_ja4_fingerprinting()->set_value(true);

    // Create the fingerprint inspector configuration.
    inspector_config_ =
        std::make_shared<QuicFingerprintInspectorConfig>(*stats_store_->rootScope(), quic_config_);

    // Create the fingerprint inspector.
    inspector_ = std::make_unique<QuicFingerprintInspector>(inspector_config_);
  }

protected:
  std::unique_ptr<Stats::IsolatedStoreImpl> stats_store_;
  envoy::config::listener::v3::QuicProtocolOptions quic_config_;
  QuicFingerprintInspectorConfigSharedPtr inspector_config_;
  std::unique_ptr<QuicFingerprintInspector> inspector_;
};

// Test fingerprint inspector configuration creation and validation.
TEST_F(QuicFingerprintInspectorTest, ConfigurationTest) {
  EXPECT_TRUE(inspector_config_->enableJA3Fingerprinting());
  EXPECT_TRUE(inspector_config_->enableJA4Fingerprinting());

  // Verify stats are available.
  EXPECT_NE(nullptr, TestUtility::findCounter(*stats_store_,
                                              "quic_fingerprint_inspector.client_hello_processed"));
  EXPECT_NE(nullptr, TestUtility::findCounter(
                         *stats_store_, "quic_fingerprint_inspector.ja3_fingerprint_created"));
  EXPECT_NE(nullptr, TestUtility::findCounter(
                         *stats_store_, "quic_fingerprint_inspector.ja4_fingerprint_created"));
  EXPECT_NE(nullptr, TestUtility::findCounter(*stats_store_,
                                              "quic_fingerprint_inspector.fingerprint_errors"));
}

TEST_F(QuicFingerprintInspectorTest, ConfigurationJA3OnlyTest) {
  envoy::config::listener::v3::QuicProtocolOptions config;
  config.mutable_enable_ja3_fingerprinting()->set_value(true);
  config.mutable_enable_ja4_fingerprinting()->set_value(false);

  auto config_ptr =
      std::make_shared<QuicFingerprintInspectorConfig>(*stats_store_->rootScope(), config);

  EXPECT_TRUE(config_ptr->enableJA3Fingerprinting());
  EXPECT_FALSE(config_ptr->enableJA4Fingerprinting());
}

TEST_F(QuicFingerprintInspectorTest, ConfigurationJA4OnlyTest) {
  envoy::config::listener::v3::QuicProtocolOptions config;
  config.mutable_enable_ja3_fingerprinting()->set_value(false);
  config.mutable_enable_ja4_fingerprinting()->set_value(true);

  auto config_ptr =
      std::make_shared<QuicFingerprintInspectorConfig>(*stats_store_->rootScope(), config);

  EXPECT_FALSE(config_ptr->enableJA3Fingerprinting());
  EXPECT_TRUE(config_ptr->enableJA4Fingerprinting());
}

TEST_F(QuicFingerprintInspectorTest, ConfigurationDisabledTest) {
  envoy::config::listener::v3::QuicProtocolOptions config;
  config.mutable_enable_ja3_fingerprinting()->set_value(false);
  config.mutable_enable_ja4_fingerprinting()->set_value(false);

  auto config_ptr =
      std::make_shared<QuicFingerprintInspectorConfig>(*stats_store_->rootScope(), config);

  EXPECT_FALSE(config_ptr->enableJA3Fingerprinting());
  EXPECT_FALSE(config_ptr->enableJA4Fingerprinting());
}

// Test error handling for null ClientHello.
TEST_F(QuicFingerprintInspectorTest, NullClientHelloTest) {
  std::string ja3_hash;
  std::string ja4_hash;

  const bool result = inspector_->processClientHello(nullptr, ja3_hash, ja4_hash);

  EXPECT_FALSE(result);
  EXPECT_TRUE(ja3_hash.empty());
  EXPECT_TRUE(ja4_hash.empty());
  EXPECT_EQ(1,
            TestUtility::findCounter(*stats_store_, "quic_fingerprint_inspector.fingerprint_errors")
                ->value());
}

// Test stats are properly incremented.
TEST_F(QuicFingerprintInspectorTest, ProcessClientHelloStatsTest) {
  std::string ja3_hash;
  std::string ja4_hash;

  // Test with null client hello - should increment error counter.
  const bool result = inspector_->processClientHello(nullptr, ja3_hash, ja4_hash);

  EXPECT_FALSE(result);
  EXPECT_EQ(1,
            TestUtility::findCounter(*stats_store_, "quic_fingerprint_inspector.fingerprint_errors")
                ->value());
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

// Test disabled configurations.
TEST_F(QuicFingerprintInspectorTest, DisabledFingerprintingTest) {
  envoy::config::listener::v3::QuicProtocolOptions config;
  config.mutable_enable_ja3_fingerprinting()->set_value(false);
  config.mutable_enable_ja4_fingerprinting()->set_value(false);

  auto disabled_config =
      std::make_shared<QuicFingerprintInspectorConfig>(*stats_store_->rootScope(), config);
  auto disabled_inspector = std::make_unique<QuicFingerprintInspector>(disabled_config);

  std::string ja3_hash;
  std::string ja4_hash;

  const bool result = disabled_inspector->processClientHello(nullptr, ja3_hash, ja4_hash);

  EXPECT_FALSE(result);
  EXPECT_TRUE(ja3_hash.empty());
  EXPECT_TRUE(ja4_hash.empty());
}

// Test factory classes.
class QuicFingerprintInspectorFactoryTest : public testing::Test {
public:
  QuicFingerprintInspectorFactoryTest()
      : stats_store_(std::make_unique<Stats::IsolatedStoreImpl>()) {}

protected:
  std::unique_ptr<Stats::IsolatedStoreImpl> stats_store_;
};

TEST_F(QuicFingerprintInspectorFactoryTest, FactoryBasicTest) {
  envoy::config::listener::v3::QuicProtocolOptions config;
  config.mutable_enable_ja3_fingerprinting()->set_value(true);
  config.mutable_enable_ja4_fingerprinting()->set_value(true);

  auto factory = std::make_unique<EnvoyQuicCryptoServerStreamFactoryImpl>();

  EXPECT_EQ("envoy.quic.crypto_stream.server.quiche", factory->name());

  factory->setQuicConfig(config);

  // Verify the factory can create a configuration protobuf.
  auto proto = factory->createEmptyConfigProto();
  EXPECT_NE(nullptr, proto);

  // Verify the factory stores the config.
  EXPECT_TRUE(factory->getQuicConfig().has_value());
  EXPECT_TRUE(PROTOBUF_GET_WRAPPED_OR_DEFAULT(factory->getQuicConfig().value(),
                                              enable_ja3_fingerprinting, false));
  EXPECT_TRUE(PROTOBUF_GET_WRAPPED_OR_DEFAULT(factory->getQuicConfig().value(),
                                              enable_ja4_fingerprinting, false));
}

// Test basic factory functionality.
TEST_F(QuicFingerprintInspectorFactoryTest, FactoryRegistrationAndName) {
  auto factory = std::make_unique<EnvoyQuicCryptoServerStreamFactoryImpl>();

  EXPECT_EQ("envoy.quic.crypto_stream.server.quiche", factory->name());

  auto proto = factory->createEmptyConfigProto();
  EXPECT_NE(nullptr, proto);

  // The factory should have a valid name.
  EXPECT_FALSE(factory->name().empty());
  EXPECT_TRUE(factory->name().find("quiche") != std::string::npos);
}

TEST_F(QuicFingerprintInspectorFactoryTest, FactoryMultipleConfigurationsTest) {
  // Test multiple factories with different configurations.
  auto factory1 = std::make_unique<EnvoyQuicCryptoServerStreamFactoryImpl>();
  auto factory2 = std::make_unique<EnvoyQuicCryptoServerStreamFactoryImpl>();

  // Configure factory1 with``JA3``only.
  envoy::config::listener::v3::QuicProtocolOptions config1;
  config1.mutable_enable_ja3_fingerprinting()->set_value(true);
  config1.mutable_enable_ja4_fingerprinting()->set_value(false);
  factory1->setQuicConfig(config1);

  // Configure factory2 with``JA4``only.
  envoy::config::listener::v3::QuicProtocolOptions config2;
  config2.mutable_enable_ja3_fingerprinting()->set_value(false);
  config2.mutable_enable_ja4_fingerprinting()->set_value(true);
  factory2->setQuicConfig(config2);

  // Verify both factories have different configurations.
  EXPECT_TRUE(factory1->getQuicConfig().has_value());
  EXPECT_TRUE(factory2->getQuicConfig().has_value());

  EXPECT_TRUE(PROTOBUF_GET_WRAPPED_OR_DEFAULT(factory1->getQuicConfig().value(),
                                              enable_ja3_fingerprinting, false));
  EXPECT_FALSE(PROTOBUF_GET_WRAPPED_OR_DEFAULT(factory1->getQuicConfig().value(),
                                               enable_ja4_fingerprinting, false));

  EXPECT_FALSE(PROTOBUF_GET_WRAPPED_OR_DEFAULT(factory2->getQuicConfig().value(),
                                               enable_ja3_fingerprinting, false));
  EXPECT_TRUE(PROTOBUF_GET_WRAPPED_OR_DEFAULT(factory2->getQuicConfig().value(),
                                              enable_ja4_fingerprinting, false));
}

TEST_F(QuicFingerprintInspectorFactoryTest, FactoryDefaultConfigurationTest) {
  // Test factory without configuration.
  auto factory = std::make_unique<EnvoyQuicCryptoServerStreamFactoryImpl>();

  // Should not have config initially.
  EXPECT_FALSE(factory->getQuicConfig().has_value());

  // Should still be able to create proto.
  auto proto = factory->createEmptyConfigProto();
  EXPECT_NE(nullptr, proto);

  // Name should be consistent.
  EXPECT_EQ("envoy.quic.crypto_stream.server.quiche", factory->name());
}

// Test inspector with different combinations of enabled/disabled settings.
class QuicFingerprintInspectorBoundaryTest : public testing::Test {
public:
  QuicFingerprintInspectorBoundaryTest()
      : stats_store_(std::make_unique<Stats::IsolatedStoreImpl>()) {}

  void SetUp() override {
    // Create a QUIC config with both fingerprinting enabled.
    quic_config_.mutable_enable_ja3_fingerprinting()->set_value(true);
    quic_config_.mutable_enable_ja4_fingerprinting()->set_value(true);

    // Create the fingerprint inspector configuration.
    inspector_config_ =
        std::make_shared<QuicFingerprintInspectorConfig>(*stats_store_->rootScope(), quic_config_);

    // Create the fingerprint inspector.
    inspector_ = std::make_unique<QuicFingerprintInspector>(inspector_config_);
  }

protected:
  std::unique_ptr<Stats::IsolatedStoreImpl> stats_store_;
  envoy::config::listener::v3::QuicProtocolOptions quic_config_;
  QuicFingerprintInspectorConfigSharedPtr inspector_config_;
  std::unique_ptr<QuicFingerprintInspector> inspector_;
};

TEST_F(QuicFingerprintInspectorBoundaryTest, ProcessMultipleNullClientHello) {
  // Test processing multiple null ClientHello messages.
  for (int i = 0; i < 10; ++i) {
    std::string ja3_hash;
    std::string ja4_hash;
    const bool result = inspector_->processClientHello(nullptr, ja3_hash, ja4_hash);
    EXPECT_FALSE(result);
    EXPECT_TRUE(ja3_hash.empty());
    EXPECT_TRUE(ja4_hash.empty());
  }

  // Verify error stats were incremented correctly.
  EXPECT_EQ(10,
            TestUtility::findCounter(*stats_store_, "quic_fingerprint_inspector.fingerprint_errors")
                ->value());
}

TEST_F(QuicFingerprintInspectorBoundaryTest, ConfigurationBoundaryConditions) {
  // Test with uninitialized bool values (should default to false).
  envoy::config::listener::v3::QuicProtocolOptions config;
  // Don't set any fingerprinting flags.

  auto config_ptr =
      std::make_shared<QuicFingerprintInspectorConfig>(*stats_store_->rootScope(), config);

  EXPECT_FALSE(config_ptr->enableJA3Fingerprinting());
  EXPECT_FALSE(config_ptr->enableJA4Fingerprinting());

  // Test with explicitly false values.
  config.mutable_enable_ja3_fingerprinting()->set_value(false);
  config.mutable_enable_ja4_fingerprinting()->set_value(false);

  auto config_ptr2 =
      std::make_shared<QuicFingerprintInspectorConfig>(*stats_store_->rootScope(), config);

  EXPECT_FALSE(config_ptr2->enableJA3Fingerprinting());
  EXPECT_FALSE(config_ptr2->enableJA4Fingerprinting());
}

TEST_F(QuicFingerprintInspectorBoundaryTest, StatsCounterValidation) {
  // Verify all expected stats counters exist.
  const std::vector<std::string> expected_counters = {
      "quic_fingerprint_inspector.ja3_fingerprint_created",
      "quic_fingerprint_inspector.ja4_fingerprint_created",
      "quic_fingerprint_inspector.client_hello_processed",
      "quic_fingerprint_inspector.fingerprint_errors"};

  for (const auto& counter_name : expected_counters) {
    EXPECT_NE(nullptr, TestUtility::findCounter(*stats_store_, counter_name))
        << "Missing counter: " << counter_name;
  }

  // Verify initial values are zero.
  for (const auto& counter_name : expected_counters) {
    EXPECT_EQ(0, TestUtility::findCounter(*stats_store_, counter_name)->value())
        << "Counter " << counter_name << " should start at zero";
  }
}

TEST_F(QuicFingerprintInspectorBoundaryTest, ConfigurationConsistency) {
  // Test that configuration is properly stored and retrieved.
  EXPECT_TRUE(inspector_config_->enableJA3Fingerprinting());
  EXPECT_TRUE(inspector_config_->enableJA4Fingerprinting());

  // Test stats are accessible.
  const auto& stats = inspector_config_->stats();
  EXPECT_EQ(0, stats.ja3_fingerprint_created_.value());
  EXPECT_EQ(0, stats.ja4_fingerprint_created_.value());
  EXPECT_EQ(0, stats.client_hello_processed_.value());
  EXPECT_EQ(0, stats.fingerprint_errors_.value());
}

// Test error handling and edge cases.
TEST_F(QuicFingerprintInspectorBoundaryTest, ErrorHandlingRobustness) {
  std::string ja3_hash;
  std::string ja4_hash;

  // Test with pre-populated output strings.
  ja3_hash = "pre_existing_ja3";
  ja4_hash = "pre_existing_ja4";

  const bool result = inspector_->processClientHello(nullptr, ja3_hash, ja4_hash);

  EXPECT_FALSE(result);
  EXPECT_TRUE(ja3_hash.empty()); // Should be cleared
  EXPECT_TRUE(ja4_hash.empty()); // Should be cleared

  // Test with same output strings multiple times.
  for (int i = 0; i < 5; ++i) {
    ja3_hash = "reset_value";
    ja4_hash = "reset_value";
    const bool result = inspector_->processClientHello(nullptr, ja3_hash, ja4_hash);
    EXPECT_FALSE(result);
    EXPECT_TRUE(ja3_hash.empty());
    EXPECT_TRUE(ja4_hash.empty());
  }

  // Verify error counter is incremented properly.
  EXPECT_EQ(6, // 1 from first call + 5 from loop
            TestUtility::findCounter(*stats_store_, "quic_fingerprint_inspector.fingerprint_errors")
                ->value());
}

} // namespace Quic
} // namespace Envoy
