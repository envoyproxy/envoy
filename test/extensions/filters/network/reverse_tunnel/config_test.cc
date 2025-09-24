#include "envoy/config/core/v3/base.pb.h"

#include "source/extensions/filters/network/reverse_tunnel/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {
namespace {

TEST(ReverseTunnelFilterConfigFactoryTest, ValidConfiguration) {
  ReverseTunnelFilterConfigFactory factory;

  const std::string yaml_string = R"EOF(
ping_interval:
  seconds: 5
auto_close_connections: false
request_path: "/custom/reverse"
request_method: PUT
)EOF";

  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(proto_config, context);
  ASSERT_TRUE(result.ok());
  Network::FilterFactoryCb cb = result.value();

  EXPECT_TRUE(cb != nullptr);

  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addReadFilter(_));
  cb(filter_manager);
}

TEST(ReverseTunnelFilterConfigFactoryTest, DefaultConfiguration) {
  ReverseTunnelFilterConfigFactory factory;

  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  // Set minimum required fields for configuration.
  proto_config.set_request_path("/reverse_connections/request");
  proto_config.set_request_method(envoy::config::core::v3::POST);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(proto_config, context);
  ASSERT_TRUE(result.ok());
  Network::FilterFactoryCb cb = result.value();

  EXPECT_TRUE(cb != nullptr);

  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addReadFilter(_));
  cb(filter_manager);
}

TEST(ReverseTunnelFilterConfigFactoryTest, ConfigProperties) {
  ReverseTunnelFilterConfigFactory factory;

  EXPECT_EQ("envoy.filters.network.reverse_tunnel", factory.name());

  ProtobufTypes::MessagePtr empty_config = factory.createEmptyConfigProto();
  EXPECT_TRUE(empty_config != nullptr);
  EXPECT_EQ("envoy.extensions.filters.network.reverse_tunnel.v3.ReverseTunnel",
            empty_config->GetTypeName());
}

TEST(ReverseTunnelFilterConfigFactoryTest, ConfigurationNoValidation) {
  ReverseTunnelFilterConfigFactory factory;

  const std::string yaml_string = R"EOF(
ping_interval:
  seconds: 1
  nanos: 500000000
auto_close_connections: true
request_path: "/test/path"
request_method: POST
)EOF";

  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(proto_config, context);
  ASSERT_TRUE(result.ok());
  Network::FilterFactoryCb cb = result.value();

  EXPECT_TRUE(cb != nullptr);

  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addReadFilter(_));
  cb(filter_manager);
}

TEST(ReverseTunnelFilterConfigFactoryTest, MinimalConfigurationYaml) {
  ReverseTunnelFilterConfigFactory factory;

  const std::string yaml_string = R"EOF(
request_path: "/minimal"
request_method: POST
)EOF";

  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(proto_config, context);
  ASSERT_TRUE(result.ok());
  Network::FilterFactoryCb cb = result.value();

  EXPECT_TRUE(cb != nullptr);

  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addReadFilter(_));
  cb(filter_manager);
}

TEST(ReverseTunnelFilterConfigFactoryTest, FactoryType) {
  ReverseTunnelFilterConfigFactory factory;

  // Test that the factory name matches expected.
  EXPECT_EQ("envoy.filters.network.reverse_tunnel", factory.name());
}

TEST(ReverseTunnelFilterConfigFactoryTest, CreateFilterFactoryFromProtoTyped) {
  ReverseTunnelFilterConfigFactory factory;

  const std::string yaml_string = R"EOF(
ping_interval:
  seconds: 3
auto_close_connections: true
request_path: "/factory/test"
request_method: PUT
)EOF";

  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(proto_config, context);
  ASSERT_TRUE(result.ok());
  Network::FilterFactoryCb cb = result.value();

  EXPECT_TRUE(cb != nullptr);

  // Test the factory callback creates the filter properly.
  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addReadFilter(_));
  cb(filter_manager);
}

} // namespace
} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
