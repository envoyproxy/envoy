#include "envoy/extensions/filters/network/zookeeper_proxy/v3/zookeeper_proxy.pb.h"
#include "envoy/extensions/filters/network/zookeeper_proxy/v3/zookeeper_proxy.pb.validate.h"

#include "source/extensions/filters/network/zookeeper_proxy/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

using ZooKeeperProxyProtoConfig =
    envoy::extensions::filters::network::zookeeper_proxy::v3::ZooKeeperProxy;

TEST(ZookeeperFilterConfigTest, ValidateFail) {
  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(
      ZooKeeperConfigFactory().createFilterFactoryFromProto(ZooKeeperProxyProtoConfig(), context),
      ProtoValidationException);
}

TEST(ZookeeperFilterConfigTest, InvalidStatPrefix) {
  const std::string yaml = R"EOF(
stat_prefix: ""
  )EOF";

  ZooKeeperProxyProtoConfig proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), ProtoValidationException);
}

TEST(ZookeeperFilterConfigTest, InvalidMaxPacketBytes) {
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
max_packet_bytes: -1
  )EOF";

  ZooKeeperProxyProtoConfig proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), EnvoyException);
}

TEST(ZookeeperFilterConfigTest, UndefinedOpcode) {
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
latency_thresholds:
  - opcode: Undefined
    threshold: 150
  )EOF";

  ZooKeeperProxyProtoConfig proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config),
               ProtobufMessage::UnknownProtoFieldException);
}

TEST(ZookeeperFilterConfigTest, NegativeLatencyThreshold) {
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
latency_thresholds:
  - opcode: Default
    threshold: -151
  )EOF";

  ZooKeeperProxyProtoConfig proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), EnvoyException);
}

TEST(ZookeeperFilterConfigTest, SimpleConfig) {
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
  )EOF";

  ZooKeeperProxyProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  ZooKeeperConfigFactory factory;

  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

TEST(ZookeeperFilterConfigTest, DuplicatedOpcodes) {
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
max_packet_bytes: 1048576
latency_thresholds:
  - opcode: Default
    threshold: 150
  - opcode: Default
    threshold: 151
  - opcode: Create
    threshold: 152
  - opcode: Create
    threshold: 153
  - opcode: Create
    threshold: 154
  - opcode: Create
    threshold: 155
  )EOF";

  ZooKeeperProxyProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  ZooKeeperConfigFactory factory;

  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

TEST(ZookeeperFilterConfigTest, FullConfig) {
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
max_packet_bytes: 1048576
latency_thresholds:
  - opcode: Default
    threshold: 150
  - opcode: Connect
    threshold: 151
  - opcode: Create
    threshold: 152
  - opcode: Delete
    threshold: 153
  - opcode: Exists
    threshold: 154
  - opcode: GetData
    threshold: 155
  - opcode: SetData
    threshold: 156
  - opcode: GetAcl
    threshold: 157
  - opcode: SetAcl
    threshold: 158
  - opcode: GetChildren
    threshold: 159
  - opcode: Sync
    threshold: 160
  - opcode: Ping
    threshold: 161
  - opcode: GetChildren2
    threshold: 162
  - opcode: Check
    threshold: 163
  - opcode: Multi
    threshold: 164
  - opcode: Create2
    threshold: 165
  - opcode: Reconfig
    threshold: 166
  - opcode: CheckWatches
    threshold: 167
  - opcode: RemoveWatches
    threshold: 168
  - opcode: CreateContainer
    threshold: 169
  - opcode: CreateTtl
    threshold: 170
  - opcode: Close
    threshold: 171
  - opcode: SetAuth
    threshold: 172
  - opcode: SetWatches
    threshold: 173
  - opcode: GetEphemerals
    threshold: 174
  - opcode: GetAllChildrenNumber
    threshold: 175
  - opcode: SetWatches2
    threshold: 176
  )EOF";

  ZooKeeperProxyProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  ZooKeeperConfigFactory factory;

  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
