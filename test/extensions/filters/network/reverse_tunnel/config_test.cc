#include "envoy/config/core/v3/base.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/cleanup.h"
#include "source/extensions/filters/network/reverse_tunnel/config.h"
#include "source/extensions/filters/network/reverse_tunnel/reverse_tunnel_filter.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {
namespace {

using ::Envoy::StatusHelpers::HasStatusMessage;

Cleanup setExtension(const envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::
                         v3::UpstreamReverseConnectionSocketInterface& config,
                     Server::Configuration::ServerFactoryContext& context) {
  auto* acceptor =
      const_cast<Extensions::Bootstrap::ReverseConnection::ReverseTunnelAcceptor*>(getAcceptor());
  RELEASE_ASSERT(acceptor != nullptr, "upstream reverse_tunnel socket interface must be linked");
  auto* prev = acceptor->extension_;
  auto* extension = new Extensions::Bootstrap::ReverseConnection::ReverseTunnelAcceptorExtension(
      *acceptor, context, config);
  acceptor->extension_ = extension;

  return Cleanup([acceptor, prev, extension] {
    acceptor->extension_ = prev;
    delete extension;
  });
}

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
  ASSERT_OK(result);
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
  ASSERT_OK(result);
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
  ASSERT_OK(result);
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
  ASSERT_OK(result);
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
  ASSERT_OK(result);
  Network::FilterFactoryCb cb = result.value();

  EXPECT_TRUE(cb != nullptr);

  // Test the factory callback creates the filter properly.
  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addReadFilter(_));
  cb(filter_manager);
}

TEST(ReverseTunnelFilterConfigFactoryTest, ConfigurationWithValidation) {
  ReverseTunnelFilterConfigFactory factory;

  const std::string yaml_string = R"EOF(
ping_interval:
  seconds: 5
auto_close_connections: false
request_path: "/reverse_connections/request"
request_method: GET
validation:
  node_id_format: "expected-node-id"
  cluster_id_format: "expected-cluster-id"
  emit_dynamic_metadata: true
  dynamic_metadata_namespace: "envoy.filters.network.reverse_tunnel"
)EOF";

  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(proto_config, context);
  ASSERT_OK(result);
  Network::FilterFactoryCb cb = result.value();

  EXPECT_TRUE(cb != nullptr);

  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addReadFilter(_));
  cb(filter_manager);
}

TEST(ReverseTunnelFilterConfigFactoryTest, ConfigurationWithStaticValidation) {
  ReverseTunnelFilterConfigFactory factory;

  const std::string yaml_string = R"EOF(
request_path: "/reverse_connections/request"
request_method: GET
validation:
  node_id_format: "expected-static-node"
  cluster_id_format: "expected-static-cluster"
)EOF";

  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(proto_config, context);
  ASSERT_OK(result);
  Network::FilterFactoryCb cb = result.value();

  EXPECT_TRUE(cb != nullptr);

  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addReadFilter(_));
  cb(filter_manager);
}

TEST(ReverseTunnelFilterConfigFactoryTest, ConfigurationWithMetadataEmission) {
  ReverseTunnelFilterConfigFactory factory;

  const std::string yaml_string = R"EOF(
request_path: "/reverse_connections/request"
request_method: GET
validation:
  node_id_format: "test-node"
  cluster_id_format: "test-cluster"
  emit_dynamic_metadata: true
  dynamic_metadata_namespace: "custom.namespace"
)EOF";

  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(proto_config, context);
  ASSERT_OK(result);
  Network::FilterFactoryCb cb = result.value();

  EXPECT_TRUE(cb != nullptr);

  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addReadFilter(_));
  cb(filter_manager);
}

TEST(ReverseTunnelFilterConfigFactoryTest, ConfigurationWithInvalidFormatter) {
  ReverseTunnelFilterConfigFactory factory;

  const std::string yaml_string = R"EOF(
request_path: "/reverse_connections/request"
request_method: GET
validation:
  node_id_format: "%INVALID_FORMATTER_COMMAND()%"
  cluster_id_format: "valid-cluster"
)EOF";

  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  auto result = factory.createFilterFactoryFromProto(proto_config, context);
  ASSERT_THAT(result, HasStatusMessage(testing::HasSubstr("Failed to parse node_id_format")));
}

TEST(ReverseTunnelFilterConfigFactoryTest, ConfigurationWithOnlyNodeIdValidation) {
  ReverseTunnelFilterConfigFactory factory;

  const std::string yaml_string = R"EOF(
request_path: "/reverse_connections/request"
request_method: GET
validation:
  node_id_format: "expected-node"
)EOF";

  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(proto_config, context);
  ASSERT_OK(result);
  Network::FilterFactoryCb cb = result.value();

  EXPECT_TRUE(cb != nullptr);

  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addReadFilter(_));
  cb(filter_manager);
}

TEST(ReverseTunnelFilterConfigFactoryTest, ConfigurationWithOnlyClusterIdValidation) {
  ReverseTunnelFilterConfigFactory factory;

  const std::string yaml_string = R"EOF(
request_path: "/reverse_connections/request"
request_method: GET
validation:
  cluster_id_format: "expected-cluster"
)EOF";

  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(proto_config, context);
  ASSERT_OK(result);
  Network::FilterFactoryCb cb = result.value();

  EXPECT_TRUE(cb != nullptr);

  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addReadFilter(_));
  cb(filter_manager);
}

TEST(ReverseTunnelFilterConfigFactoryTest, ConfigurationWithOnlyTenantIdValidation) {
  ReverseTunnelFilterConfigFactory factory;

  const std::string yaml_string = R"EOF(
request_path: "/reverse_connections/request"
request_method: GET
validation:
  tenant_id_format: "expected-tenant"
)EOF";

  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(proto_config, context);
  ASSERT_OK(result);
  Network::FilterFactoryCb cb = result.value();

  EXPECT_TRUE(cb != nullptr);

  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addReadFilter(_));
  cb(filter_manager);
}

TEST(ReverseTunnelFilterConfigFactoryTest, ConfigurationWithInvalidTenantIdFormatter) {
  ReverseTunnelFilterConfigFactory factory;

  const std::string yaml_string = R"EOF(
request_path: "/reverse_connections/request"
request_method: GET
validation:
  node_id_format: "valid-node"
  cluster_id_format: "valid-cluster"
  tenant_id_format: "%INVALID_FORMATTER_COMMAND()%"
)EOF";

  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  auto result = factory.createFilterFactoryFromProto(proto_config, context);
  ASSERT_THAT(result, HasStatusMessage(testing::HasSubstr("Failed to parse tenant_id_format")));
}

// Tests that the ReverseTunnelFilterConfig is formed properly and the filter construction works.
TEST(ReverseTunnelFilterConfigFactoryTest, ConfigurationSkipRebalancingEnabled) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  proto_config.set_skip_rebalancing(true);
  proto_config.set_request_path("/request");
  proto_config.set_request_method(envoy::config::core::v3::POST);

  ReverseTunnelFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(proto_config, context);
  ASSERT_OK(result);
  Network::FilterFactoryCb cb = result.value();
  EXPECT_TRUE(cb != nullptr);

  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addReadFilter(_));
  cb(filter_manager);
}

TEST(ReverseTunnelFilterConfigFactoryTest, ConnectionLimitRejectedWithoutBootstrapExtension) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  proto_config.set_enable_connection_limit(true);

  ReverseTunnelFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(proto_config, context);
  ASSERT_THAT(result, HasStatusMessage(testing::HasSubstr("enable_connection_limit")));
}

TEST(ReverseTunnelFilterConfigFactoryTest, ConnectionLimitRejectedWhenCapIsSetToZero) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  proto_config.set_enable_connection_limit(true);

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface extension_config;
  extension_config.set_max_connections_per_node(0);
  auto cleanup = setExtension(extension_config, server_context);

  ReverseTunnelFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(proto_config, context);
  ASSERT_THAT(result, HasStatusMessage(testing::HasSubstr("max_connections_per_node")));
}

TEST(ReverseTunnelFilterConfigFactoryTest, ConnectionLimitAcceptedWhenCapIsGreaterThanZero) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  proto_config.set_enable_connection_limit(true);

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface extension_config;
  extension_config.set_max_connections_per_node(1);
  auto cleanup = setExtension(extension_config, server_context);

  ReverseTunnelFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(proto_config, context);
  ASSERT_OK(result);
  Network::FilterFactoryCb cb = result.value();
  EXPECT_TRUE(cb != nullptr);

  Network::MockFilterManager filter_manager;
  EXPECT_CALL(filter_manager, addReadFilter(_));
  cb(filter_manager);
}

} // namespace
} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
