#include <string>

#include "test/mocks/server/listener_factory_context.h"
#include "test/test_common/utility.h"

#include "contrib/envoy/extensions/filters/listener/postgres_inspector/v3alpha/postgres_inspector.pb.h"
#include "contrib/envoy/extensions/filters/listener/postgres_inspector/v3alpha/postgres_inspector.pb.validate.h"
#include "contrib/postgres_inspector/filters/listener/source/config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace PostgresInspector {
namespace {

TEST(ConfigTest, CreateEmptyConfigProto) {
  PostgresInspectorConfigFactory factory;
  ProtobufTypes::MessagePtr config = factory.createEmptyConfigProto();

  EXPECT_NE(nullptr, config);
  EXPECT_NE(
      nullptr,
      dynamic_cast<
          envoy::extensions::filters::listener::postgres_inspector::v3alpha::PostgresInspector*>(
          config.get()));
}

TEST(ConfigTest, FilterName) {
  PostgresInspectorConfigFactory factory;
  EXPECT_EQ("envoy.filters.listener.postgres_inspector", factory.name());
}

TEST(PostgresInspectorConfigTest, DefaultConfig) {
  Stats::IsolatedStoreImpl stats_store;
  envoy::extensions::filters::listener::postgres_inspector::v3alpha::PostgresInspector proto_config;

  Config config(stats_store, proto_config);

  EXPECT_TRUE(config.enableMetadataExtraction());
  EXPECT_EQ(Config::DEFAULT_MAX_STARTUP_MESSAGE_SIZE, config.maxStartupMessageSize());
  EXPECT_EQ(std::chrono::milliseconds(Config::DEFAULT_STARTUP_TIMEOUT_MS), config.startupTimeout());
}

TEST(PostgresInspectorConfigTest, CustomConfig) {
  Stats::IsolatedStoreImpl stats_store;
  envoy::extensions::filters::listener::postgres_inspector::v3alpha::PostgresInspector proto_config;

  proto_config.mutable_enable_metadata_extraction()->set_value(false);
  proto_config.mutable_max_startup_message_size()->set_value(2048);
  proto_config.mutable_startup_timeout()->set_seconds(5);

  Config config(stats_store, proto_config);

  EXPECT_FALSE(config.enableMetadataExtraction());
  EXPECT_EQ(2048, config.maxStartupMessageSize());
  EXPECT_EQ(std::chrono::milliseconds(5000), config.startupTimeout());
}

TEST(PostgresInspectorConfigTest, StatsInitialization) {
  Stats::IsolatedStoreImpl stats_store;
  envoy::extensions::filters::listener::postgres_inspector::v3alpha::PostgresInspector proto_config;

  Config config(stats_store, proto_config);

  // Check that stats are properly initialized.
  const auto& stats = config.stats();

  // These counters should exist and be set as zero initially.
  EXPECT_EQ(0, stats.postgres_found_.value());
  EXPECT_EQ(0, stats.postgres_not_found_.value());
  EXPECT_EQ(0, stats.ssl_requested_.value());
  EXPECT_EQ(0, stats.ssl_not_requested_.value());
  EXPECT_EQ(0, stats.startup_message_too_large_.value());
  EXPECT_EQ(0, stats.startup_message_timeout_.value());
  EXPECT_EQ(0, stats.protocol_error_.value());
}

} // namespace
} // namespace PostgresInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
