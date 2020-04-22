#include "envoy/extensions/filters/network/zookeeper_proxy/v3/zookeeper_proxy.pb.h"
#include "envoy/extensions/filters/network/zookeeper_proxy/v3/zookeeper_proxy.pb.validate.h"

#include "extensions/filters/network/zookeeper_proxy/config.h"

#include "test/mocks/server/mocks.h"
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

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
