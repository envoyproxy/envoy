#include "extensions/filters/network/kafka/broker/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

TEST(KafkaConfigFactoryUnitTest, shouldCreateFilter) {
  // given
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
  )EOF";

  KafkaBrokerProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  KafkaConfigFactory factory;

  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));

  // when
  cb(connection);

  // then - connection had `addFilter` invoked
}

TEST(KafkaConfigFactoryUnitTest, shouldThrowOnInvalidStatPrefix) {
  // given
  const std::string yaml = R"EOF(
stat_prefix: ""
  )EOF";

  KafkaBrokerProtoConfig proto_config;

  // when
  // then - exception gets thrown
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), ProtoValidationException);
}

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
