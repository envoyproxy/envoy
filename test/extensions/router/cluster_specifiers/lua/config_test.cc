#include "source/extensions/router/cluster_specifiers/lua/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace Lua {

TEST(LuaClusterSpecifierPluginConfigTest, EmptyConfig) {
  LuaClusterSpecifierPluginFactoryConfig factory;

  ProtobufTypes::MessagePtr empty_config = factory.createEmptyConfigProto();
  EXPECT_NE(nullptr, empty_config);
}

TEST(LuaClusterSpecifierPluginConfigTest, NormalConfig) {
  const std::string normal_lua_config_yaml = R"EOF(
  source_code:
    inline_string: |
      function envoy_on_route(route_handle)
        local header_value = route_handle:headers():get("header_key")
        if header_value == "fake" then
          return "fake_service"
        end
        return "web_service"
      end
  default_cluster: default_service
  )EOF";

  LuaClusterSpecifierConfigProto proto_config{};
  TestUtility::loadFromYaml(normal_lua_config_yaml, proto_config);
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  LuaClusterSpecifierPluginFactoryConfig factory;
  Envoy::Router::ClusterSpecifierPluginSharedPtr plugin =
      factory.createClusterSpecifierPlugin(proto_config, context);
  EXPECT_NE(nullptr, plugin);
}

TEST(LuaClusterSpecifierPluginConfigTest, NoOnRouteConfig) {
  const std::string normal_lua_config_yaml = R"EOF(
  source_code:
    inline_string: |
      function envoy_on_no_route(route_handle)
        local header_value = route_handle:headers():get("header_key")
        if header_value == "fake" then
          return "fake_service"
        end
        return "web_service"
      end
  default_cluster: default_service
  )EOF";

  LuaClusterSpecifierConfigProto proto_config{};
  TestUtility::loadFromYaml(normal_lua_config_yaml, proto_config);
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  LuaClusterSpecifierPluginFactoryConfig factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createClusterSpecifierPlugin(proto_config, context), Envoy::EnvoyException,
      "envoy_on_route() function not found. Lua will not hook cluster specifier.");
}

} // namespace Lua
} // namespace Router
} // namespace Extensions
} // namespace Envoy
