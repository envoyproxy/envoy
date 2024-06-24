#include "test/mocks/server/factory_context.h"
#include "test/mocks/thread/mocks.h"

#include "contrib/kafka/filters/network/source/mesh/config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

TEST(KafkaMeshConfigFactoryUnitTest, shouldCreateFilter) {
  // given
  const std::string yaml = R"EOF(
advertised_host: "127.0.0.1"
advertised_port: 19092
upstream_clusters:
- cluster_name: kafka_c1
  bootstrap_servers: 127.0.0.1:9092
  partition_count: 1
- cluster_name: kafka_c2
  bootstrap_servers: 127.0.0.1:9093
  partition_count: 1
- cluster_name: kafka_c3
  bootstrap_servers: 127.0.0.1:9094
  partition_count: 5
  producer_config:
    acks: "1"
    linger.ms: "500"
forwarding_rules:
- target_cluster: kafka_c1
  topic_prefix: apples
- target_cluster: kafka_c2
  topic_prefix: bananas
- target_cluster: kafka_c3
  topic_prefix: cherries
  )EOF";

  KafkaMeshProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  testing::NiceMock<Thread::MockThreadFactory> thread_factory;
  ON_CALL(context.server_factory_context_.api_, threadFactory())
      .WillByDefault(ReturnRef(thread_factory));
  KafkaMeshConfigFactory factory;

  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context).value();
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));

  // when
  cb(connection);

  // then - connection had `addFilter` invoked
}

TEST(KafkaMeshConfigFactoryUnitTest, throwsIfAdvertisedPortIsMissing) {
  // given
  const std::string yaml = R"EOF(
advertised_host: "127.0.0.1"
  )EOF";

  KafkaMeshProtoConfig proto_config;

  // when
  // then - exception gets thrown
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), ProtoValidationException);
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
