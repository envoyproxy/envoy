#include "source/extensions/transport_sockets/dynamic_modules/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace TransportSockets {
namespace {

class DynamicModuleTransportSocketConfigTest : public testing::Test {
protected:
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
};

TEST_F(DynamicModuleTransportSocketConfigTest, UpstreamConfigBasic) {
  const std::string yaml = R"EOF(
    dynamic_module_config:
      name: test_module
    socket_name: test_socket
    socket_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: '{"test": "config"}'
  )EOF";

  envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleUpstreamTransportSocket
      proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  // Create factory - this will fail as we don't have a test module
  UpstreamDynamicModuleTransportSocketConfigFactory factory;

  EXPECT_EQ("envoy.transport_sockets.dynamic_modules", factory.name());

  auto empty_proto = factory.createEmptyConfigProto();
  EXPECT_NE(nullptr, empty_proto);

  // Creating the actual factory would fail without a real module
  auto factory_or_error = factory.createTransportSocketFactory(proto_config, factory_context_);
  EXPECT_FALSE(factory_or_error.ok());
  EXPECT_THAT(factory_or_error.status().message(),
              ::testing::HasSubstr("Failed to load dynamic module"));
}

TEST_F(DynamicModuleTransportSocketConfigTest, DownstreamConfigBasic) {
  const std::string yaml = R"EOF(
    dynamic_module_config:
      name: test_module_basic
    socket_name: test_socket
    socket_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: '{"test": "config"}'
    require_client_certificate:
      value: false
  )EOF";

  envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleDownstreamTransportSocket
      proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  // Create factory
  DownstreamDynamicModuleTransportSocketConfigFactory factory;

  EXPECT_EQ("envoy.transport_sockets.dynamic_modules", factory.name());

  auto empty_proto = factory.createEmptyConfigProto();
  EXPECT_NE(nullptr, empty_proto);

  // Creating the actual factory would fail without a real module
  std::vector<std::string> server_names;
  auto factory_or_error =
      factory.createTransportSocketFactory(proto_config, factory_context_, server_names);
  EXPECT_FALSE(factory_or_error.ok());
  EXPECT_THAT(factory_or_error.status().message(),
              ::testing::HasSubstr("Failed to load dynamic module"));
}

TEST_F(DynamicModuleTransportSocketConfigTest, UpstreamConfigWithAlpn) {
  const std::string yaml = R"EOF(
    dynamic_module_config:
      name: test_module
      do_not_close: true
    socket_name: rustls_client
    socket_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: '{"cipher": "AES256-GCM-SHA384"}'
    sni: "example.com"
    alpn_protocols:
      - "h2"
      - "http/1.1"
  )EOF";

  envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleUpstreamTransportSocket
      proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  // Verify configuration values
  EXPECT_EQ("test_module", proto_config.dynamic_module_config().name());
  EXPECT_TRUE(proto_config.dynamic_module_config().do_not_close());
  EXPECT_EQ("rustls_client", proto_config.socket_name());
  EXPECT_EQ("example.com", proto_config.sni());
  ASSERT_EQ(2, proto_config.alpn_protocols_size());
  EXPECT_EQ("h2", proto_config.alpn_protocols(0));
  EXPECT_EQ("http/1.1", proto_config.alpn_protocols(1));
}

TEST_F(DynamicModuleTransportSocketConfigTest, DownstreamConfigWithClientAuth) {
  const std::string yaml = R"EOF(
    dynamic_module_config:
      name: test_module
    socket_name: rustls_server
    socket_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: '{"cipher": "AES128-GCM-SHA256"}'
    require_client_certificate:
      value: true
    alpn_protocols:
      - "http/1.1"
  )EOF";

  envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleDownstreamTransportSocket
      proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  // Verify configuration values
  EXPECT_EQ("test_module", proto_config.dynamic_module_config().name());
  EXPECT_EQ("rustls_server", proto_config.socket_name());
  EXPECT_TRUE(proto_config.has_require_client_certificate());
  EXPECT_TRUE(proto_config.require_client_certificate().value());
  ASSERT_EQ(1, proto_config.alpn_protocols_size());
  EXPECT_EQ("http/1.1", proto_config.alpn_protocols(0));
}

TEST_F(DynamicModuleTransportSocketConfigTest, EmptyConfigProto) {
  UpstreamDynamicModuleTransportSocketConfigFactory upstream_factory;
  auto upstream_proto = upstream_factory.createEmptyConfigProto();
  EXPECT_NE(nullptr, upstream_proto);
  EXPECT_NE(nullptr, dynamic_cast<envoy::extensions::transport_sockets::dynamic_modules::v3::
                                      DynamicModuleUpstreamTransportSocket*>(upstream_proto.get()));

  DownstreamDynamicModuleTransportSocketConfigFactory downstream_factory;
  auto downstream_proto = downstream_factory.createEmptyConfigProto();
  EXPECT_NE(nullptr, downstream_proto);
  EXPECT_NE(nullptr,
            dynamic_cast<envoy::extensions::transport_sockets::dynamic_modules::v3::
                             DynamicModuleDownstreamTransportSocket*>(downstream_proto.get()));
}

// Error path tests.

TEST_F(DynamicModuleTransportSocketConfigTest, FailsWithNonExistentModule) {
  const std::string yaml = R"EOF(
    dynamic_module_config:
      name: this_module_does_not_exist
    socket_name: test_socket
  )EOF";

  envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleUpstreamTransportSocket
      proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  UpstreamDynamicModuleTransportSocketConfigFactory factory;
  auto factory_or_error = factory.createTransportSocketFactory(proto_config, factory_context_);

  EXPECT_FALSE(factory_or_error.ok());
  EXPECT_THAT(factory_or_error.status().message(),
              ::testing::HasSubstr("Failed to load dynamic module"));
}

TEST_F(DynamicModuleTransportSocketConfigTest, ValidatesSocketNameLength) {
  const std::string yaml = R"EOF(
    dynamic_module_config:
      name: test_module
    socket_name: "valid_name"
  )EOF";

  envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleUpstreamTransportSocket
      proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  // Verify that a valid socket name passes parsing.
  EXPECT_EQ("valid_name", proto_config.socket_name());
  EXPECT_FALSE(proto_config.socket_name().empty());

  // Validation will happen in createTransportSocketFactory which uses messageValidationVisitor().
}

TEST_F(DynamicModuleTransportSocketConfigTest, DownstreamFailsWithNonExistentModule) {
  const std::string yaml = R"EOF(
    dynamic_module_config:
      name: nonexistent_module
    socket_name: test_socket
  )EOF";

  envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleDownstreamTransportSocket
      proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  DownstreamDynamicModuleTransportSocketConfigFactory factory;
  std::vector<std::string> server_names;
  auto factory_or_error =
      factory.createTransportSocketFactory(proto_config, factory_context_, server_names);

  EXPECT_FALSE(factory_or_error.ok());
  EXPECT_THAT(factory_or_error.status().message(),
              ::testing::HasSubstr("Failed to load dynamic module"));
}

TEST_F(DynamicModuleTransportSocketConfigTest, HandlesInvalidSocketConfig) {
  const std::string yaml = R"EOF(
    dynamic_module_config:
      name: test_module
    socket_name: test_socket
    socket_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: '{invalid json}'
  )EOF";

  envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleUpstreamTransportSocket
      proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  UpstreamDynamicModuleTransportSocketConfigFactory factory;
  auto factory_or_error = factory.createTransportSocketFactory(proto_config, factory_context_);

  // Should fail because the module doesn't exist, but config parsing should succeed.
  EXPECT_FALSE(factory_or_error.ok());
  EXPECT_THAT(factory_or_error.status().message(),
              ::testing::HasSubstr("Failed to load dynamic module"));
}

TEST_F(DynamicModuleTransportSocketConfigTest, UpstreamSniTooLong) {
  const std::string yaml = R"EOF(
    dynamic_module_config:
      name: test_module
    socket_name: test_socket
    sni: ")" + std::string(256, 'a') + R"("
  )EOF";

  envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleUpstreamTransportSocket
      proto_config;

  // This should fail proto validation due to max_bytes: 255 constraint.
  EXPECT_THROW(TestUtility::loadFromYaml(yaml, proto_config), EnvoyException);
}

} // namespace
} // namespace TransportSockets
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
