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
latency_threshold_overrides:
  - opcode: Undefined
    threshold:
      nanos: 150000000
  )EOF";

  ZooKeeperProxyProtoConfig proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config),
               ProtobufMessage::UnknownProtoFieldException);
}

TEST(ZookeeperFilterConfigTest, NegativeLatencyThreshold) {
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
latency_threshold_overrides:
  - opcode: Connect
    threshold:
      nanos: -150000000
  )EOF";

  ZooKeeperProxyProtoConfig proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), EnvoyException);
}

TEST(ZookeeperFilterConfigTest, TooSmallLatencyThreshold) {
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
latency_threshold_overrides:
  - opcode: Multi
    threshold:
      nanos: 999999
  )EOF";

  ZooKeeperProxyProtoConfig proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), EnvoyException);
}

TEST(ZookeeperFilterConfigTest, UnsetLatencyThreshold) {
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
latency_threshold_overrides:
  - opcode: Create
    threshold:
  )EOF";

  ZooKeeperProxyProtoConfig proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), EnvoyException);
}

TEST(ZookeeperFilterConfigTest, DuplicatedOpcodes) {
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
max_packet_bytes: 1048576
latency_threshold_overrides:
  - opcode: Sync
    threshold:
      nanos: 150000000
  - opcode: Sync
    threshold:
      nanos: 151000000
  - opcode: Create
    threshold:
      nanos: 152000000
  - opcode: Create
    threshold:
      nanos: 153000000
  - opcode: Create
    threshold:
      nanos: 154000000
  - opcode: Create
    threshold:
      nanos: 155000000
  )EOF";

  ZooKeeperProxyProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(ZooKeeperConfigFactory().createFilterFactoryFromProto(proto_config, context),
               EnvoyException);
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

TEST(ZookeeperFilterConfigTest, ConfigWithDefaultLatencyThreshold) {
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
default_latency_threshold: "0.15s"
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

TEST(ZookeeperFilterConfigTest, ConfigWithConnectLatencyThreshold) {
  const std::string yaml = R"EOF(
stat_prefix: test_prefix
latency_threshold_overrides:
  - opcode: Connect
    threshold: "0.151s"
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
default_latency_threshold: "0.1s"
latency_threshold_overrides:
  - opcode: Connect
    threshold: "0.151s"
  - opcode: Create
    threshold: "0.152s"
  - opcode: Delete
    threshold: "0.153s"
  - opcode: Exists
    threshold:
      nanos: 154000000
  - opcode: GetData
    threshold:
      nanos: 155000000
  - opcode: SetData
    threshold:
      nanos: 156000000
  - opcode: GetAcl
    threshold:
      nanos: 157000000
  - opcode: SetAcl
    threshold:
      nanos: 158000000
  - opcode: GetChildren
    threshold:
      nanos: 159000000
  - opcode: Sync
    threshold:
      nanos: 160000000
  - opcode: Ping
    threshold:
      nanos: 161000000
  - opcode: GetChildren2
    threshold:
      nanos: 162000000
  - opcode: Check
    threshold:
      nanos: 163000000
  - opcode: Multi
    threshold:
      nanos: 164000000
  - opcode: Create2
    threshold:
      nanos: 165000000
  - opcode: Reconfig
    threshold:
      nanos: 166000000
  - opcode: CheckWatches
    threshold:
      nanos: 167000000
  - opcode: RemoveWatches
    threshold:
      nanos: 168000000
  - opcode: CreateContainer
    threshold:
      nanos: 169000000
  - opcode: CreateTtl
    threshold:
      nanos: 170000000
  - opcode: Close
    threshold:
      nanos: 171000000
  - opcode: SetAuth
    threshold:
      nanos: 172000000
  - opcode: SetWatches
    threshold:
      nanos: 173000000
  - opcode: GetEphemerals
    threshold:
      nanos: 174000000
  - opcode: GetAllChildrenNumber
    threshold:
      nanos: 175000000
  - opcode: SetWatches2
    threshold:
      nanos: 176000000
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
