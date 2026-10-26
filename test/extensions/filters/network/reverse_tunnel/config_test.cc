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

// Fixture that installs a valid upstream reverse tunnel acceptor extension, which the filter
// factory requires, so each test exercises the filter proto rather than the extension wiring.
class ReverseTunnelFilterConfigFactoryTest : public testing::Test {
protected:
  void SetUp() override {
    acceptor_ =
        const_cast<Extensions::Bootstrap::ReverseConnection::ReverseTunnelAcceptor*>(getAcceptor());
    RELEASE_ASSERT(acceptor_ != nullptr, "upstream reverse_tunnel socket interface must be linked");
    envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
        UpstreamReverseConnectionSocketInterface extension_config;
    extension_config.set_max_connections_per_node(100);
    prev_extension_ = acceptor_->extension_;
    extension_ =
        std::make_unique<Extensions::Bootstrap::ReverseConnection::ReverseTunnelAcceptorExtension>(
            *acceptor_, server_context_, extension_config);
    acceptor_->extension_ = extension_.get();
  }

  void TearDown() override { acceptor_->extension_ = prev_extension_; }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  Extensions::Bootstrap::ReverseConnection::ReverseTunnelAcceptor* acceptor_{nullptr};
  Extensions::Bootstrap::ReverseConnection::ReverseTunnelAcceptorExtension* prev_extension_{
      nullptr};
  std::unique_ptr<Extensions::Bootstrap::ReverseConnection::ReverseTunnelAcceptorExtension>
      extension_;
};

TEST_F(ReverseTunnelFilterConfigFactoryTest, ValidConfiguration) {
  ReverseTunnelFilterConfigFactory factory;

  const std::string yaml_string = R"EOF(
ping_interval:
  seconds: 5
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

TEST_F(ReverseTunnelFilterConfigFactoryTest, DefaultConfiguration) {
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

TEST_F(ReverseTunnelFilterConfigFactoryTest, ConfigProperties) {
  ReverseTunnelFilterConfigFactory factory;

  EXPECT_EQ("envoy.filters.network.reverse_tunnel", factory.name());

  ProtobufTypes::MessagePtr empty_config = factory.createEmptyConfigProto();
  EXPECT_TRUE(empty_config != nullptr);
  EXPECT_EQ("envoy.extensions.filters.network.reverse_tunnel.v3.ReverseTunnel",
            empty_config->GetTypeName());
}

TEST_F(ReverseTunnelFilterConfigFactoryTest, ConfigurationNoValidation) {
  ReverseTunnelFilterConfigFactory factory;

  const std::string yaml_string = R"EOF(
ping_interval:
  seconds: 1
  nanos: 500000000
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

TEST_F(ReverseTunnelFilterConfigFactoryTest, MinimalConfigurationYaml) {
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

TEST_F(ReverseTunnelFilterConfigFactoryTest, FactoryType) {
  ReverseTunnelFilterConfigFactory factory;

  // Test that the factory name matches expected.
  EXPECT_EQ("envoy.filters.network.reverse_tunnel", factory.name());
}

TEST_F(ReverseTunnelFilterConfigFactoryTest, CreateFilterFactoryFromProtoTyped) {
  ReverseTunnelFilterConfigFactory factory;

  const std::string yaml_string = R"EOF(
ping_interval:
  seconds: 3
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

TEST_F(ReverseTunnelFilterConfigFactoryTest, ConfigurationWithValidation) {
  ReverseTunnelFilterConfigFactory factory;

  const std::string yaml_string = R"EOF(
ping_interval:
  seconds: 5
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

TEST_F(ReverseTunnelFilterConfigFactoryTest, ConfigurationWithStaticValidation) {
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

TEST_F(ReverseTunnelFilterConfigFactoryTest, ConfigurationWithMetadataEmission) {
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

TEST_F(ReverseTunnelFilterConfigFactoryTest, ConfigurationWithInvalidFormatter) {
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

TEST_F(ReverseTunnelFilterConfigFactoryTest, ConfigurationWithOnlyNodeIdValidation) {
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

TEST_F(ReverseTunnelFilterConfigFactoryTest, ConfigurationWithOnlyClusterIdValidation) {
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

TEST_F(ReverseTunnelFilterConfigFactoryTest, ConfigurationWithOnlyTenantIdValidation) {
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

TEST_F(ReverseTunnelFilterConfigFactoryTest, ConfigurationWithInvalidTenantIdFormatter) {
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
TEST_F(ReverseTunnelFilterConfigFactoryTest, ConfigurationSkipRebalancingEnabled) {
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

// Without the upstream reverse tunnel acceptor bootstrap extension the filter cannot register
// tunnels, so the factory rejects it at config load. This test deliberately omits the fixture so
// no extension is installed.
TEST(ReverseTunnelFilterConfigFactoryNoExtensionTest, FilterRejectedWithoutBootstrapExtension) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  proto_config.set_request_path("/reverse_connections/request");
  proto_config.set_request_method(envoy::config::core::v3::GET);

  ReverseTunnelFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(proto_config, context);
  ASSERT_THAT(result,
              HasStatusMessage(testing::HasSubstr("UpstreamReverseConnectionSocketInterface")));
}

TEST_F(ReverseTunnelFilterConfigFactoryTest, ConnectionLimitRejectedWhenCapIsSetToZero) {
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

TEST_F(ReverseTunnelFilterConfigFactoryTest, ConnectionLimitAcceptedWhenCapIsGreaterThanZero) {
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
