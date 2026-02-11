#include "envoy/extensions/transport_sockets/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/extensions/transport_sockets/dynamic_modules/factory.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"

#include "gmock/gmock.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {

class DynamicModuleTransportSocketConfigFactoryTest : public testing::Test {
public:
  void SetUp() override {
    Envoy::Extensions::DynamicModules::DynamicModulesTestEnvironment::setModulesSearchPath();
  }

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
};

// Test upstream config factory with valid config.
TEST_F(DynamicModuleTransportSocketConfigFactoryTest, UpstreamValidConfig) {
  UpstreamDynamicModuleTransportSocketConfigFactory factory;
  EXPECT_EQ("envoy.transport_sockets.dynamic_modules", factory.name());

  envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleUpstreamTransportSocket
      config;
  config.mutable_dynamic_module_config()->set_name("transport_socket_no_op");
  config.set_socket_name("test_socket");
  config.set_sni("example.com");
  config.add_alpn_protocols("h2");

  auto result = factory.createTransportSocketFactory(config, context_);
  EXPECT_TRUE(result.ok()) << result.status().message();
}

// Test downstream config factory with valid config.
TEST_F(DynamicModuleTransportSocketConfigFactoryTest, DownstreamValidConfig) {
  DownstreamDynamicModuleTransportSocketConfigFactory factory;
  EXPECT_EQ("envoy.transport_sockets.dynamic_modules", factory.name());

  envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleDownstreamTransportSocket
      config;
  config.mutable_dynamic_module_config()->set_name("transport_socket_no_op");
  config.set_socket_name("test_socket");
  config.mutable_require_client_certificate()->set_value(true);
  config.add_alpn_protocols("h2");

  std::vector<std::string> server_names;
  auto result = factory.createTransportSocketFactory(config, context_, server_names);
  EXPECT_TRUE(result.ok()) << result.status().message();
}

// Test upstream config factory with invalid module name.
TEST_F(DynamicModuleTransportSocketConfigFactoryTest, UpstreamInvalidModuleName) {
  UpstreamDynamicModuleTransportSocketConfigFactory factory;

  envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleUpstreamTransportSocket
      config;
  config.mutable_dynamic_module_config()->set_name("nonexistent_module");
  config.set_socket_name("test_socket");

  auto result = factory.createTransportSocketFactory(config, context_);
  EXPECT_FALSE(result.ok());
}

// Test downstream config factory with invalid module name.
TEST_F(DynamicModuleTransportSocketConfigFactoryTest, DownstreamInvalidModuleName) {
  DownstreamDynamicModuleTransportSocketConfigFactory factory;

  envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleDownstreamTransportSocket
      config;
  config.mutable_dynamic_module_config()->set_name("nonexistent_module");
  config.set_socket_name("test_socket");

  std::vector<std::string> server_names;
  auto result = factory.createTransportSocketFactory(config, context_, server_names);
  EXPECT_FALSE(result.ok());
}

// Test upstream config factory with a module that lacks transport socket symbols.
TEST_F(DynamicModuleTransportSocketConfigFactoryTest, UpstreamModuleMissingSymbols) {
  UpstreamDynamicModuleTransportSocketConfigFactory factory;

  envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleUpstreamTransportSocket
      config;
  // Use the HTTP-only no_op module which lacks transport socket symbols.
  config.mutable_dynamic_module_config()->set_name("no_op");
  config.set_socket_name("test_socket");

  auto result = factory.createTransportSocketFactory(config, context_);
  EXPECT_FALSE(result.ok());
}

// Test createEmptyConfigProto returns correct type for upstream.
TEST_F(DynamicModuleTransportSocketConfigFactoryTest, UpstreamEmptyConfigProto) {
  UpstreamDynamicModuleTransportSocketConfigFactory factory;
  auto proto = factory.createEmptyConfigProto();
  EXPECT_NE(nullptr, proto);
  EXPECT_NE(nullptr, dynamic_cast<envoy::extensions::transport_sockets::dynamic_modules::v3::
                                      DynamicModuleUpstreamTransportSocket*>(proto.get()));
}

// Test createEmptyConfigProto returns correct type for downstream.
TEST_F(DynamicModuleTransportSocketConfigFactoryTest, DownstreamEmptyConfigProto) {
  DownstreamDynamicModuleTransportSocketConfigFactory factory;
  auto proto = factory.createEmptyConfigProto();
  EXPECT_NE(nullptr, proto);
  EXPECT_NE(nullptr, dynamic_cast<envoy::extensions::transport_sockets::dynamic_modules::v3::
                                      DynamicModuleDownstreamTransportSocket*>(proto.get()));
}

} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
