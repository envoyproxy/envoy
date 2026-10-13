#include "test/mocks/server/server_factory_context.h"

#include "contrib/envoy/extensions/stat_sinks/kafka/v3/kafka_stats_sink.pb.h"
#include "contrib/kafka/stat_sinks/source/config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Kafka {
namespace {

TEST(KafkaStatsSinkFactoryTest, CreateEmptyConfigProto) {
  KafkaStatsSinkFactory factory;
  auto proto = factory.createEmptyConfigProto();
  EXPECT_NE(nullptr, proto);
  EXPECT_NE(nullptr, dynamic_cast<envoy::extensions::stat_sinks::kafka::v3::KafkaStatsSinkConfig*>(
                         proto.get()));
}

TEST(KafkaStatsSinkFactoryTest, Name) {
  KafkaStatsSinkFactory factory;
  EXPECT_EQ("envoy.stat_sinks.kafka", factory.name());
}

TEST(KafkaStatsSinkFactoryTest, ConfigProtoAcceptsJsonFormat) {
  envoy::extensions::stat_sinks::kafka::v3::KafkaStatsSinkConfig config;
  config.set_broker_list("localhost:9092");
  config.set_topic("metrics");
  config.set_format(envoy::extensions::stat_sinks::kafka::v3::JSON);
  EXPECT_EQ(envoy::extensions::stat_sinks::kafka::v3::JSON, config.format());
}

TEST(KafkaStatsSinkFactoryTest, ConfigProtoAcceptsProtobufFormat) {
  envoy::extensions::stat_sinks::kafka::v3::KafkaStatsSinkConfig config;
  config.set_broker_list("localhost:9092");
  config.set_topic("metrics");
  config.set_format(envoy::extensions::stat_sinks::kafka::v3::PROTOBUF);
  EXPECT_EQ(envoy::extensions::stat_sinks::kafka::v3::PROTOBUF, config.format());
}

TEST(KafkaStatsSinkFactoryTest, ConfigProtoDefaultsToJson) {
  envoy::extensions::stat_sinks::kafka::v3::KafkaStatsSinkConfig config;
  EXPECT_EQ(envoy::extensions::stat_sinks::kafka::v3::JSON, config.format());
}

TEST(KafkaStatsSinkFactoryTest, EmitTagsAsLabelsDefaultsToUnset) {
  envoy::extensions::stat_sinks::kafka::v3::KafkaStatsSinkConfig config;
  EXPECT_FALSE(config.has_emit_tags_as_labels());
}

} // namespace
} // namespace Kafka
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
